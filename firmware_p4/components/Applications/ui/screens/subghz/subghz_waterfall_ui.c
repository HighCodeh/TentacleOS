// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "subghz_waterfall_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "frequency_waterfall.h"
#include "frequency_waterfall_color.h"
#include "frequency_catalog.h"
#include "led_control.h"
#include "power_policy.h"
#include "subghz_menu_ui.h"
#include "subghz_settings.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define HISTORY_ROWS      64
#define MARGIN            8
#define GAP               4
#define LABEL_HEIGHT      16
#define CONTROL_HEIGHT    32
#define UPDATE_MS         50
#define START_TIMEOUT_MS  3000
#define DBM_FLOOR         (-120)
#define DBM_CEILING       (-20)
#define DEFAULT_CENTER_HZ 433920000U
#define DEFAULT_SPAN_HZ   2000000U
#define DEFAULT_BANDWIDTH 3
#define SCALE_UNIT        256
#define HZ_PER_MHZ        1000000U
#define HZ_PER_KHZ        1000U
static const char *TAG = "WATERFALL_UI";
static const uint32_t SPANS[] = {
    100000, 250000, 500000, 1000000, 2000000, 5000000, 10000000, 20000000};
typedef enum {
  SUBGHZ_WATERFALL_VIEW,
  SUBGHZ_WATERFALL_CENTER,
  SUBGHZ_WATERFALL_SPAN,
  SUBGHZ_WATERFALL_BANDWIDTH,
  SUBGHZ_WATERFALL_CURSOR,
  SUBGHZ_WATERFALL_HOLD,
  SUBGHZ_WATERFALL_PAUSE,
  SUBGHZ_WATERFALL_RAW,
  SUBGHZ_WATERFALL_CONTROL_COUNT
} subghz_waterfall_control_t;
typedef struct {
  uint16_t pixels[FREQUENCY_WATERFALL_BINS * HISTORY_ROWS];
  float history[FREQUENCY_WATERFALL_BINS * HISTORY_ROWS];
  frequency_waterfall_color_t colors;
  lv_point_precise_t live[FREQUENCY_WATERFALL_BINS];
  lv_point_precise_t hold[FREQUENCY_WATERFALL_BINS];
  float maximum[FREQUENCY_WATERFALL_BINS];
  frequency_waterfall_snapshot_t data;
} subghz_waterfall_buffer_t;
static subghz_waterfall_buffer_t *s_buffer;
static lv_color_t s_theme_colors[5];
static uint32_t s_requested_center_hz = DEFAULT_CENTER_HZ;
static frequency_waterfall_config_t s_config = {
    .center_hz = DEFAULT_CENTER_HZ, .span_hz = DEFAULT_SPAN_HZ, .bandwidth = DEFAULT_BANDWIDTH};
static lv_obj_t *s_summary;
static lv_point_precise_t s_grid_points[4][2];
static lv_obj_t *s_screen, *s_info, *s_status, *s_axis, *s_control, *s_footer;
static lv_obj_t *s_plot, *s_live, *s_hold, *s_cursor_line, *s_canvas;
static lv_point_precise_t s_cursor_points[2];
static lv_timer_t *s_timer;
static uint32_t s_started, s_sequence, s_rows, s_missed;
static uint16_t s_rendered_cursor;
static uint16_t s_cursor = FREQUENCY_WATERFALL_BINS / 2;
static uint8_t s_selected;
static bool s_pending, s_owns, s_paused, s_max_hold, s_raw, s_error;
static int32_t s_plot_width, s_plot_height;
static void tick(lv_timer_t *timer);
static void input(const input_event_t *event, void *context);

void ui_subghz_waterfall_set_center(uint32_t frequency_hz) {
  if (frequency_waterfall_fit_center(&s_config, frequency_hz) == ESP_OK)
    s_requested_center_hz = frequency_hz;
}
static void fail(const char *message) {
  if (!s_error) {
    ESP_LOGE(TAG, "%s", message);
    led_signal_error();
  }
  s_error = true;
  s_pending = s_raw = false;
  if (s_owns)
    frequency_waterfall_stop();
  lv_label_set_text(s_status, message);
  power_policy_set_display_hold(s_screen, false);
}
void ui_subghz_waterfall_stop(void) {
  s_pending = s_raw = false;
  if (s_owns)
    frequency_waterfall_stop();
  s_owns = false;
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (s_screen != NULL)
    power_policy_set_display_hold(s_screen, false);
  if (s_canvas != NULL) {
    lv_obj_delete(s_canvas);
    s_canvas = NULL;
  }
  if (s_live != NULL) {
    lv_line_set_points(s_live, NULL, 0);
    s_live = NULL;
  }
  if (s_hold != NULL) {
    lv_line_set_points(s_hold, NULL, 0);
    s_hold = NULL;
  }
  free(s_buffer);
  s_buffer = NULL;
}
static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen) {
    ui_subghz_waterfall_stop();
    s_screen = NULL;
  }
}
static lv_obj_t *label(int32_t y, int32_t height) {
  lv_obj_t *obj = lv_label_create(s_screen);
  lv_obj_set_pos(obj, MARGIN, y);
  lv_obj_set_size(obj, ui_screen_w() - 2 * MARGIN, height);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(obj, current_theme.text_main, 0);
  return obj;
}
static void format_label(lv_obj_t *obj, const char *format, ...) {
  char text[128];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (strcmp(lv_label_get_text(obj), text) != 0)
    lv_label_set_text(obj, text);
}
static int32_t dbm_y(float dbm) {
  float bounded = LV_CLAMP(DBM_FLOOR, dbm, DBM_CEILING);
  return (DBM_CEILING - bounded) * (s_plot_height - 1) / (DBM_CEILING - DBM_FLOOR);
}
static uint16_t heat_color(float dbm) {
  uint8_t strength = frequency_waterfall_color_level(&s_buffer->colors, dbm);
  return lv_color_to_u16(ui_theme_get_waterfall_color(strength));
}
static void refresh_controls(void) {
  format_label(s_summary,
               "C%.3f S%.2fM BW%.0fk",
               (double)s_config.center_hz / HZ_PER_MHZ,
               (double)s_config.span_hz / HZ_PER_MHZ,
               (double)frequency_waterfall_bandwidth(s_config.bandwidth));
  static const char *const names[] = {
      "View", "Center", "Span", "RX bandwidth", "Cursor", "Max hold", "Pause", "Read RAW"};
  switch (s_selected) {
    case SUBGHZ_WATERFALL_CENTER:
      format_label(s_control,
                   "%s: %lu.%03lu MHz\nL/R Radio Config frequencies",
                   names[s_selected],
                   (unsigned long)(s_requested_center_hz / HZ_PER_MHZ),
                   (unsigned long)(s_requested_center_hz % HZ_PER_MHZ / HZ_PER_KHZ));
      break;
    case SUBGHZ_WATERFALL_SPAN:
      format_label(s_control,
                   "Span: %lu kHz\nL/R change / clears history",
                   (unsigned long)(s_config.span_hz / HZ_PER_KHZ));
      break;
    case SUBGHZ_WATERFALL_BANDWIDTH:
      format_label(s_control,
                   "RX bandwidth: %.1f kHz\nL/R change / clears history",
                   (double)frequency_waterfall_bandwidth(s_config.bandwidth));
      break;
    case SUBGHZ_WATERFALL_CURSOR: {
      uint32_t hz = frequency_waterfall_bin_hz(&s_config, s_cursor);
      if (s_sequence)
        format_label(s_control,
                     "Cursor: %lu.%03lu MHz / %.1f dBm\nL/R move / OK pause",
                     (unsigned long)(hz / HZ_PER_MHZ),
                     (unsigned long)(hz % HZ_PER_MHZ / HZ_PER_KHZ),
                     (double)s_buffer->data.dbm[s_cursor]);
      else
        lv_label_set_text(s_control, "Cursor: waiting for a full sweep\nL/R move / OK pause");
      break;
    }
    case SUBGHZ_WATERFALL_HOLD:
      format_label(s_control, "Max hold: %s\nL/R toggle / dashed trace", s_max_hold ? "ON" : "OFF");
      break;
    case SUBGHZ_WATERFALL_PAUSE:
      format_label(s_control,
                   "Pause: %s\nOK or L/R %s",
                   s_paused ? "ON" : "OFF",
                   s_paused ? "resume" : "pause");
      break;
    case SUBGHZ_WATERFALL_RAW:
      lv_label_set_text(s_control, "Read RAW at cursor\nOK open / stops waterfall first");
      break;
    default:
      lv_label_set_text(s_control, "View: Waterfall\nLEFT/RIGHT return to Finder");
      break;
  }
  if (s_rendered_cursor == s_cursor)
    return;
  s_rendered_cursor = s_cursor;
  int32_t x = (uint32_t)s_cursor * (s_plot_width - 1) / (FREQUENCY_WATERFALL_BINS - 1);
  s_cursor_points[0] = (lv_point_precise_t){x, 0};
  s_cursor_points[1] = (lv_point_precise_t){x, s_plot_height - 1};
  lv_line_set_points(s_cursor_line, s_cursor_points, 2);
}
static void reset_history(void) {
  s_sequence = s_rows = s_missed = 0;
  memset(&s_buffer->data, 0, sizeof(s_buffer->data));
  frequency_waterfall_color_reset(&s_buffer->colors);
  for (unsigned i = 0; i < FREQUENCY_WATERFALL_BINS * HISTORY_ROWS; ++i)
    s_buffer->pixels[i] = heat_color(DBM_FLOOR);
  for (unsigned i = 0; i < FREQUENCY_WATERFALL_BINS; ++i) {
    s_buffer->maximum[i] = DBM_FLOOR;
    s_buffer->live[i] = (lv_point_precise_t){
        i * (s_plot_width - 1) / (FREQUENCY_WATERFALL_BINS - 1), s_plot_height - 1};
    s_buffer->hold[i] = s_buffer->live[i];
  }
  lv_obj_invalidate(s_canvas);
  lv_obj_invalidate(s_plot);
}
static void tick(lv_timer_t *timer) {
  (void)timer;
  lv_color_t colors[] = {current_theme.screen_base,
                         current_theme.bg_secondary,
                         current_theme.border_accent,
                         current_theme.text_main,
                         current_theme.text_secondary};
  for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
    if (!lv_color_eq(colors[i], s_theme_colors[i])) {
      ui_switch_screen(SCREEN_SUBGHZ_WATERFALL);
      return;
    }
  }
  if (s_error || s_buffer == NULL)
    return;
  if (s_raw) {
    if (!frequency_waterfall_is_running()) {
      s_owns = false;
      subghz_settings_set_freq(frequency_waterfall_bin_hz(&s_config, s_cursor));
      ui_subghz_read_set_raw(true);
      ui_switch_screen(SCREEN_SUBGHZ_READ);
    } else if (lv_tick_elaps(s_started) > START_TIMEOUT_MS)
      fail("Radio cleanup failed / BACK");
    return;
  }
  if (s_pending) {
    if (s_owns && frequency_waterfall_is_running()) {
      if (lv_tick_elaps(s_started) > START_TIMEOUT_MS)
        fail("Radio cleanup failed / BACK");
      return;
    }
    s_owns = false;
    esp_err_t error = frequency_waterfall_start(&s_config);
    if (error != ESP_OK) {
      if (error != ESP_ERR_INVALID_STATE || lv_tick_elaps(s_started) > START_TIMEOUT_MS)
        fail("Radio unavailable / BACK");
      return;
    }
    s_owns = true;
    s_pending = false;
    frequency_waterfall_pause(s_paused);
  }
  frequency_waterfall_snapshot_t line;
  frequency_waterfall_get_snapshot(&line);
  if (line.error != ESP_OK) {
    fail("Radio read failed / BACK");
    return;
  }
  if (line.sequence != s_sequence) {
    s_missed += line.sequence - s_sequence - 1;
    s_sequence = line.sequence;
    s_buffer->data = line;
    if (s_rows < HISTORY_ROWS)
      ++s_rows;
    if (frequency_waterfall_color_update(&s_buffer->colors, line.dbm, FREQUENCY_WATERFALL_BINS) !=
        ESP_OK) {
      fail("Invalid RSSI / BACK");
      return;
    }
    memmove(s_buffer->history + FREQUENCY_WATERFALL_BINS,
            s_buffer->history,
            sizeof(s_buffer->history) - FREQUENCY_WATERFALL_BINS * sizeof(float));
    memcpy(s_buffer->history, line.dbm, sizeof(line.dbm));
    for (unsigned i = 0; i < s_rows * FREQUENCY_WATERFALL_BINS; ++i)
      s_buffer->pixels[i] = heat_color(s_buffer->history[i]);
    unsigned peak = 0;
    for (unsigned i = 0; i < FREQUENCY_WATERFALL_BINS; ++i) {
      if (line.dbm[i] > s_buffer->maximum[i])
        s_buffer->maximum[i] = line.dbm[i];
      s_buffer->live[i].y = dbm_y(line.dbm[i]);
      s_buffer->hold[i].y = dbm_y(s_buffer->maximum[i]);
      if (line.dbm[i] > line.dbm[peak])
        peak = i;
    }
    uint32_t hz = frequency_waterfall_bin_hz(&s_config, peak);
    format_label(s_info,
                 "Peak %lu.%03lu MHz / %.1f dBm",
                 (unsigned long)(hz / HZ_PER_MHZ),
                 (unsigned long)(hz % HZ_PER_MHZ / HZ_PER_KHZ),
                 (double)line.dbm[peak]);
    lv_obj_invalidate(s_canvas);
    lv_obj_invalidate(s_plot);
  }
  format_label(s_status,
               "Auto %.0f/%.0f dBm | %lums%s",
               (double)s_buffer->colors.floor_dbm,
               (double)s_buffer->colors.ceiling_dbm,
               (unsigned long)(s_buffer->data.duration_us / HZ_PER_KHZ),
               s_paused ? " P" : "");
  refresh_controls();
}
static void input(const input_event_t *event, void *context) {
  (void)context;
  bool is_press = event->action == INPUT_ACTION_PRESS;
  bool is_nav = is_press || event->action == INPUT_ACTION_REPEAT;
  if (is_press && event->button == INPUT_BTN_BACK) {
    ui_switch_screen(SCREEN_SUBGHZ_SPECTRUM);
    return;
  }
  if (!is_nav || s_error || s_raw || s_buffer == NULL)
    return;
  int direction = event->button == INPUT_BTN_LEFT ? -1 : 1;
  if (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN) {
    s_selected =
        (s_selected + SUBGHZ_WATERFALL_CONTROL_COUNT + (event->button == INPUT_BTN_UP ? -1 : 1)) %
        SUBGHZ_WATERFALL_CONTROL_COUNT;
  } else if (is_press && event->button == INPUT_BTN_OK && s_selected == SUBGHZ_WATERFALL_RAW) {
    if (!s_sequence || s_pending) {
      lv_label_set_text(s_status, "Wait for a complete sweep");
      return;
    }
    s_raw = true;
    s_started = lv_tick_get();
    frequency_waterfall_stop();
    lv_label_set_text(s_status, "Opening RAW reader...");
  } else if ((is_press && event->button == INPUT_BTN_OK) ||
             ((event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT) &&
              s_selected == SUBGHZ_WATERFALL_PAUSE)) {
    s_paused = !s_paused;
    frequency_waterfall_pause(s_paused);
  } else if (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT) {
    if (s_selected == SUBGHZ_WATERFALL_VIEW) {
      ui_switch_screen(SCREEN_SUBGHZ_SPECTRUM);
      return;
    }
    if (s_selected == SUBGHZ_WATERFALL_CURSOR) {
      s_cursor = (s_cursor + FREQUENCY_WATERFALL_BINS + direction) % FREQUENCY_WATERFALL_BINS;
    } else if (s_selected == SUBGHZ_WATERFALL_HOLD) {
      s_max_hold = !s_max_hold;
      if (s_max_hold)
        lv_obj_remove_flag(s_hold, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(s_hold, LV_OBJ_FLAG_HIDDEN);
    } else if (s_selected != SUBGHZ_WATERFALL_RAW) {
      frequency_waterfall_config_t config = s_config;
      uint32_t requested_center = s_requested_center_hz;
      if (s_selected == SUBGHZ_WATERFALL_CENTER) {
        unsigned index = frequency_catalog_nearest(requested_center);
        index =
            (index + FREQUENCY_CATALOG_CARRIER_COUNT + direction) % FREQUENCY_CATALOG_CARRIER_COUNT;
        requested_center = frequency_catalog_get(NULL)[index];
        frequency_waterfall_fit_center(&config, requested_center);
      }
      if (s_selected == SUBGHZ_WATERFALL_BANDWIDTH)
        config.bandwidth = (config.bandwidth + frequency_waterfall_bandwidth_count() + direction) %
                           frequency_waterfall_bandwidth_count();
      if (s_selected == SUBGHZ_WATERFALL_SPAN) {
        unsigned index = 0, count = sizeof(SPANS) / sizeof(SPANS[0]);
        while (index + 1 < count && SPANS[index] < config.span_hz)
          ++index;
        config.span_hz = SPANS[(index + count + direction) % count];
        frequency_waterfall_fit_center(&config, requested_center);
      }
      if (frequency_waterfall_validate(&config) != ESP_OK) {
        lv_label_set_text(s_status, "Range crosses unsupported RF band");
        return;
      }
      s_config = config;
      s_requested_center_hz = requested_center;
      reset_history();
      if (s_owns)
        frequency_waterfall_stop();
      s_pending = true;
      s_started = lv_tick_get();
      lv_label_set_text(s_info, "Waiting for complete sweep");
      uint32_t start = frequency_waterfall_bin_hz(&s_config, 0),
               end = frequency_waterfall_bin_hz(&s_config, FREQUENCY_WATERFALL_BINS - 1);
      format_label(s_axis,
                   "%lu.%03lu - %lu.%03lu MHz",
                   (unsigned long)(start / HZ_PER_MHZ),
                   (unsigned long)(start % HZ_PER_MHZ / HZ_PER_KHZ),
                   (unsigned long)(end / HZ_PER_MHZ),
                   (unsigned long)(end % HZ_PER_MHZ / HZ_PER_KHZ));
    }
  }
  refresh_controls();
  ui_feedback(UI_FB_NAV);
}
void ui_subghz_waterfall_open(void) {
  ui_subghz_waterfall_stop();
  if (s_screen != NULL)
    lv_obj_delete(s_screen);
  s_screen = ui_chrome_screen_create();
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  s_theme_colors[0] = current_theme.screen_base;
  s_theme_colors[1] = current_theme.bg_secondary;
  s_theme_colors[2] = current_theme.border_accent;
  s_theme_colors[3] = current_theme.text_main;
  s_theme_colors[4] = current_theme.text_secondary;
  s_error = s_paused = s_max_hold = false;
  s_selected = SUBGHZ_WATERFALL_CENTER;
  s_rendered_cursor = FREQUENCY_WATERFALL_BINS;
  ui_chrome_header(s_screen, "SPECTRUM / WATERFALL", "/assets/icons/graphic_eq.bin");
  int32_t top = UI_CHROME_HEADER_H + GAP;
  s_summary = label(top, LABEL_HEIGHT);
  top += LABEL_HEIGHT;
  s_info = label(top, LABEL_HEIGHT);
  top += LABEL_HEIGHT;
  s_status = label(top, LABEL_HEIGHT);
  top += LABEL_HEIGHT;
  s_plot_width = ui_screen_w() - 2 * MARGIN;
  int32_t controls_y = ui_screen_h() - UI_CHROME_FOOTER_H - CONTROL_HEIGHT - GAP;
  int32_t available = controls_y - top - LABEL_HEIGHT - GAP;
  s_plot_height = available / 2;
  s_plot = lv_obj_create(s_screen);
  lv_obj_remove_style_all(s_plot);
  lv_obj_set_pos(s_plot, MARGIN, top);
  lv_obj_set_size(s_plot, s_plot_width, s_plot_height);
  lv_obj_remove_flag(s_plot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_plot, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(s_plot, LV_OPA_COVER, 0);
  for (unsigned i = 0; i < 4; ++i) {
    int32_t y = i * (s_plot_height - 1) / 3;
    s_grid_points[i][0] = (lv_point_precise_t){0, y};
    s_grid_points[i][1] = (lv_point_precise_t){s_plot_width - 1, y};
    lv_obj_t *grid = lv_line_create(s_plot);
    lv_line_set_points(grid, s_grid_points[i], 2);
    lv_obj_set_style_line_color(grid, current_theme.border_inactive, 0);
    lv_obj_set_style_line_opa(grid, LV_OPA_40, 0);
  }
  s_live = lv_line_create(s_plot);
  s_hold = lv_line_create(s_plot);
  s_cursor_line = lv_line_create(s_plot);
  lv_obj_set_style_line_color(s_live, current_theme.border_accent, 0);
  lv_obj_set_style_line_color(s_hold, current_theme.text_secondary, 0);
  lv_obj_set_style_line_dash_width(s_hold, GAP, 0);
  lv_obj_set_style_line_dash_gap(s_hold, GAP, 0);
  lv_obj_add_flag(s_hold, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_line_color(s_cursor_line, current_theme.text_main, 0);
  for (unsigned i = 0; i < 2; ++i) {
    lv_obj_t *scale = lv_label_create(s_plot);
    lv_label_set_text(scale, i == 0 ? "-20" : "-120");
    lv_obj_set_style_text_font(scale, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(scale, current_theme.text_secondary, 0);
    lv_obj_align(scale, i == 0 ? LV_ALIGN_TOP_LEFT : LV_ALIGN_BOTTOM_LEFT, 0, 0);
  }
  top += s_plot_height;
  s_axis = label(top, LABEL_HEIGHT);
  top += LABEL_HEIGHT;
  uint32_t start = frequency_waterfall_bin_hz(&s_config, 0),
           end = frequency_waterfall_bin_hz(&s_config, FREQUENCY_WATERFALL_BINS - 1);
  format_label(s_axis,
               "%lu.%03lu - %lu.%03lu MHz",
               (unsigned long)(start / HZ_PER_MHZ),
               (unsigned long)(start % HZ_PER_MHZ / HZ_PER_KHZ),
               (unsigned long)(end / HZ_PER_MHZ),
               (unsigned long)(end % HZ_PER_MHZ / HZ_PER_KHZ));
  s_control = label(controls_y, CONTROL_HEIGHT);
  lv_obj_set_style_text_color(s_control, current_theme.border_accent, 0);
  s_footer = ui_chrome_footer(s_screen, "U/D field  L/R edit  OK  BACK");
  lv_label_set_text(s_info, "Waiting for complete sweep");
  lv_label_set_text(s_status, "Starting / BACK to Finder");
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  s_buffer = heap_caps_calloc(1, sizeof(*s_buffer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_buffer == NULL) {
    fail("PSRAM unavailable / BACK");
    goto cleanup;
  }
  s_canvas = lv_canvas_create(s_screen);
  lv_canvas_set_buffer(
      s_canvas, s_buffer->pixels, FREQUENCY_WATERFALL_BINS, HISTORY_ROWS, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(s_canvas, MARGIN, top);
  lv_image_set_pivot(s_canvas, 0, 0);
  lv_image_set_scale_x(s_canvas, s_plot_width * SCALE_UNIT / FREQUENCY_WATERFALL_BINS);
  lv_image_set_scale_y(s_canvas, (controls_y - GAP - top) * SCALE_UNIT / HISTORY_ROWS);
  lv_line_set_points(s_live, s_buffer->live, FREQUENCY_WATERFALL_BINS);
  lv_line_set_points(s_hold, s_buffer->hold, FREQUENCY_WATERFALL_BINS);
  reset_history();
  refresh_controls();
  s_timer = lv_timer_create(tick, UPDATE_MS, NULL);
  if (s_timer == NULL) {
    fail("Timer unavailable / BACK");
    goto cleanup;
  }
  s_started = lv_tick_get();
  s_pending = true;
  power_policy_set_display_hold(s_screen, true);
  return;
cleanup:
  ui_subghz_waterfall_stop();
}
