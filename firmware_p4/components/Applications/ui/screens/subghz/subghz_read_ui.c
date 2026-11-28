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

#include "subghz_menu_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "capture_result_ui.h"
#include "keyboard_ui.h"
#include "led_control.h"
#include "notify_ui.h"
#include "subghz_receiver.h"
#include "subghz_send_ui.h"
#include "subghz_settings.h"
#include "subghz_storage.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

static bool s_is_raw;

static const char *TAG = "SUBGHZ_RD";

#define READ_POLL_MS        50
#define CONTENT_PAD         10
#define CONTENT_GAP         6
#define GRAPH_PAD           8
#define GRAPH_RADIUS        10
#define GRAPH_LINE_WIDTH    2
#define GRAPH_GRID_LINES    3
#define GRAPH_GRID_DIVISOR  4
#define GRAPH_RSSI_MIN      (-120)
#define GRAPH_RSSI_MAX      (-20)
#define MHZ_SCALE           1000000UL
#define MHZ_DECIMALS        10000UL
#define FREQUENCY_TEXT_SIZE 24
#define TEXT_SIZE           96
#define MS_PER_SECOND       1000U
#define MS_PER_TENTH        100U

static lv_obj_t *s_screen;
static lv_obj_t *s_content;
static lv_obj_t *s_status;
static lv_obj_t *s_freq;
static lv_obj_t *s_stats;
static lv_obj_t *s_graph;
static lv_obj_t *s_trace;
static lv_obj_t *s_grid[GRAPH_GRID_LINES];
static lv_obj_t *s_hint;
static lv_timer_t *s_tick_timer;
static bool s_options;
static bool s_saved;
static bool s_has_rx_error;
static bool s_owns_rx;
static subghz_rx_result_t s_result;
static capture_result_t s_cr;
static lv_point_precise_t s_trace_points[SUBGHZ_RX_RSSI_SAMPLES];
static lv_point_precise_t s_grid_points[GRAPH_GRID_LINES][2];
static char s_card_sub[TEXT_SIZE];
static char s_card_value[TEXT_SIZE];

static void read_tick_cb(lv_timer_t *timer);
static void subghz_read_input(const input_event_t *event, void *context);

static void stop_rx(void) {
  if (s_owns_rx) {
    subghz_receiver_stop();
    s_owns_rx = false;
  }
}

static void screen_deleted_cb(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen)
    ui_subghz_read_stop();
}

static void fmt_mhz(uint32_t hz, char *out, size_t size) {
  if (!hz) {
    snprintf(out, size, "Hopping bands");
    return;
  }
  snprintf(out,
           size,
           "%lu.%02lu MHz",
           (unsigned long)(hz / MHZ_SCALE),
           (unsigned long)((hz % MHZ_SCALE) / MHZ_DECIMALS));
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, lv_pct(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  return label;
}

static void draw_rssi(const subghz_rx_progress_t *progress) {
  lv_obj_update_layout(s_screen);
  int width = lv_obj_get_content_width(s_graph);
  int height = lv_obj_get_content_height(s_graph);
  if (width <= 1 || height <= 1)
    return;
  for (int i = 0; i < GRAPH_GRID_LINES; ++i) {
    int y = (height - 1) * (i + 1) / GRAPH_GRID_DIVISOR;
    s_grid_points[i][0] = (lv_point_precise_t){0, y};
    s_grid_points[i][1] = (lv_point_precise_t){width - 1, y};
    lv_line_set_points(s_grid[i], s_grid_points[i], 2);
  }
  uint32_t count = progress->rssi_count;
  if (count > SUBGHZ_RX_RSSI_SAMPLES)
    count = SUBGHZ_RX_RSSI_SAMPLES;
  for (uint32_t i = 0; i < count; ++i) {
    int value = progress->rssi_dbm[i];
    value = LV_CLAMP(GRAPH_RSSI_MIN, value, GRAPH_RSSI_MAX);
    s_trace_points[i].x =
        (width - 1) * (SUBGHZ_RX_RSSI_SAMPLES - count + i) / (SUBGHZ_RX_RSSI_SAMPLES - 1);
    s_trace_points[i].y =
        (height - 1) * (GRAPH_RSSI_MAX - value) / (GRAPH_RSSI_MAX - GRAPH_RSSI_MIN);
  }
  if (count > 1)
    lv_line_set_points(s_trace, s_trace_points, count);
}

void ui_subghz_read_set_raw(bool is_raw) {
  s_is_raw = is_raw;
}

void ui_subghz_read_open(void) {
  ui_subghz_read_stop();
  lv_obj_t *previous = s_screen;
  s_screen = NULL;
  if (previous != NULL)
    lv_obj_delete(previous);
  s_options = s_saved = s_has_rx_error = false;
  s_result = (subghz_rx_result_t){0};
  s_cr = (capture_result_t){0};
  subghz_settings_t config = subghz_settings_get();
  s_screen = ui_chrome_screen_create();
  lv_obj_add_event_cb(s_screen, screen_deleted_cb, LV_EVENT_DELETE, NULL);
  ui_chrome_header(s_screen, "READ", "/assets/icons/sensors.bin");
  s_content = lv_obj_create(s_screen);
  lv_obj_set_pos(s_content, 0, UI_CHROME_HEADER_H);
  lv_obj_set_size(
      s_content, ui_screen_w(), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_content, 0, 0);
  lv_obj_set_style_pad_all(s_content, CONTENT_PAD, 0);
  lv_obj_set_style_pad_row(s_content, CONTENT_GAP, 0);
  lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
  s_status = make_label(s_content, "Listening for a signal", current_theme.text_main);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  char frequency[FREQUENCY_TEXT_SIZE];
  fmt_mhz(config.freq, frequency, sizeof(frequency));
  s_freq = make_label(s_content, frequency, current_theme.border_accent);
  s_graph = lv_obj_create(s_content);
  lv_obj_set_width(s_graph, lv_pct(100));
  lv_obj_set_height(s_graph, 0);
  lv_obj_set_flex_grow(s_graph, 1);
  lv_obj_remove_flag(s_graph, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(s_graph, GRAPH_PAD, 0);
  lv_obj_set_style_bg_color(s_graph, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(s_graph, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_graph, current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(s_graph, 1, 0);
  lv_obj_set_style_radius(s_graph, GRAPH_RADIUS, 0);
  for (int i = 0; i < GRAPH_GRID_LINES; ++i) {
    s_grid[i] = lv_line_create(s_graph);
    lv_obj_set_style_line_color(s_grid[i], current_theme.border_inactive, 0);
    lv_obj_set_style_line_opa(s_grid[i], LV_OPA_30, 0);
  }
  s_trace = lv_line_create(s_graph);
  lv_obj_set_style_line_color(s_trace, current_theme.border_accent, 0);
  lv_obj_set_style_line_width(s_trace, GRAPH_LINE_WIDTH, 0);
  lv_obj_set_style_line_rounded(s_trace, true, 0);
  s_stats =
      make_label(s_content, "RSSI history / waiting for pulses", current_theme.text_secondary);
  s_hint = ui_chrome_footer(s_screen, s_is_raw ? "OK finish   BACK cancel" : "BACK cancel");
  s_tick_timer = lv_timer_create(read_tick_cb, READ_POLL_MS, NULL);
  ui_input_set_screen_handler(subghz_read_input, NULL);
  esp_err_t error =
      s_tick_timer != NULL
          ? subghz_receiver_start_configured(
                s_is_raw ? SUBGHZ_MODE_RAW : SUBGHZ_MODE_SCAN, &config.radio, config.freq)
          : ESP_ERR_NO_MEM;
  s_owns_rx = error == ESP_OK;
  if (error != ESP_OK) {
    s_has_rx_error = true;
    ESP_LOGE(TAG, "Cannot start reception: %s", esp_err_to_name(error));
    led_signal_error();
    lv_label_set_text(s_status, "Cannot start reception");
  }
  ui_screen_load_owned(&s_screen, s_screen);
  subghz_rx_progress_t empty = {0};
  draw_rssi(&empty);
}

static void show_result(void) {
  stop_rx();
  lv_obj_delete(s_content);
  s_content = NULL;
  s_graph = s_trace = s_status = s_freq = s_stats = NULL;
  char frequency[FREQUENCY_TEXT_SIZE];
  fmt_mhz(s_result.freq, frequency, sizeof(frequency));
  if (s_result.decoded) {
    snprintf(s_card_sub,
             sizeof(s_card_sub),
             "%s / Key %08lX / %u bits",
             s_result.data.protocol_name,
             (unsigned long)s_result.data.raw_value,
             (unsigned)s_result.data.bit_count);
  } else {
    snprintf(s_card_sub, sizeof(s_card_sub), "Unknown protocol / RAW waveform");
  }
  snprintf(s_card_value,
           sizeof(s_card_value),
           "%s / %lu pulses%s",
           frequency,
           (unsigned long)s_result.pulse_count,
           s_result.is_limited ? " / LIMITED" : "");
  capture_result_cfg_t config = {
      .accent = current_theme.border_accent,
      .card_icon = "/assets/icons/graphic_eq.bin",
      .card_title = s_result.is_limited ? "Capture limit reached"
                    : s_result.decoded  ? s_result.data.protocol_name
                                        : "Signal captured",
      .card_sub = s_card_sub,
      .card_value = s_card_value,
      .primary_label = "Send",
      .again_label = "Capture again",
  };
  s_cr = capture_result_create(s_screen, &config);
  s_options = true;
  ui_chrome_footer_set_text(s_hint, "UP/DOWN choose   OK select   BACK exit");
  ESP_LOGI(TAG,
           "Capture analyzed: %lu pulses, %lu blocks, %lu matches%s",
           (unsigned long)s_result.pulse_count,
           (unsigned long)s_result.frame_count,
           (unsigned long)s_result.matching_frames,
           s_result.is_limited ? " (limited)" : "");
  ui_feedback(UI_FB_READ);
}

static void read_tick_cb(lv_timer_t *timer) {
  (void)timer;
  if (lv_screen_active() != s_screen) {
    ui_subghz_read_stop();
    return;
  }
  if (s_options)
    return;
  if (subghz_receiver_get_result(&s_result)) {
    show_result();
    return;
  }
  if (s_has_rx_error)
    return;
  if (subghz_receiver_get_error() != ESP_OK) {
    s_has_rx_error = true;
    stop_rx();
    lv_label_set_text(s_status, "Reception failed / BACK to exit");
    return;
  }
  subghz_rx_progress_t progress;
  if (!subghz_receiver_get_progress(&progress))
    return;
  lv_label_set_text(s_status,
                    progress.phase == SUBGHZ_RX_PHASE_CAPTURING   ? "Receiving / collecting pulses"
                    : progress.phase == SUBGHZ_RX_PHASE_ANALYZING ? "Analyzing captured signal"
                    : progress.rejected_bursts                    ? "Noise filtered / listening"
                                                                  : "Listening for a signal");
  char frequency[FREQUENCY_TEXT_SIZE];
  fmt_mhz(progress.frequency_hz, frequency, sizeof(frequency));
  if (progress.rssi_count)
    lv_label_set_text_fmt(
        s_freq, "%s  /  %d dBm", frequency, (int)progress.rssi_dbm[progress.rssi_count - 1]);
  lv_label_set_text_fmt(s_stats,
                        "%lu pulses / %lu blocks / %lu.%lus",
                        (unsigned long)progress.pulse_count,
                        (unsigned long)progress.frame_count,
                        (unsigned long)(progress.elapsed_ms / MS_PER_SECOND),
                        (unsigned long)((progress.elapsed_ms % MS_PER_SECOND) / MS_PER_TENTH));
  draw_rssi(&progress);
}

static void send_current(void) {
  if (s_saved) {
    ui_subghz_send_select(s_result.save_name);
  } else {
    esp_err_t error = ui_subghz_send_capture(s_result.seq);
    if (error != ESP_OK) {
      ESP_LOGE(TAG, "Cannot prepare capture: %s", esp_err_to_name(error));
      led_signal_error();
      notify(NOTIFY_WARNING, "Cannot prepare signal for sending");
      return;
    }
  }
  ui_switch_screen(SCREEN_SUBGHZ_SEND);
}

static void save_name_submit(const char *name, void *context) {
  (void)context;
  esp_err_t err = subghz_receiver_save(s_result.seq, name);
  if (err == ESP_OK) {
    snprintf(s_result.save_name, sizeof(s_result.save_name), "%s", name);
    s_saved = true;
    capture_result_mark_saved(&s_cr);
    ESP_LOGI(TAG, "Capture saved: %s", name);
    led_signal_info();
    ui_feedback(UI_FB_WRITE);
    notify(NOTIFY_SAVED, "Saved to SD");
  } else {
    ESP_LOGE(TAG, "Capture save failed: %s", esp_err_to_name(err));
    led_signal_error();
    notify(NOTIFY_WARNING,
           err == ESP_ERR_INVALID_STATE ? "Name exists; choose another"
                                        : "Capture not saved; check SD");
    keyboard_open_filename(
        "Signal name (without .sub)", name, sizeof(s_result.save_name) - 1, save_name_submit);
  }
}

static void subghz_read_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (ev->button == INPUT_BTN_BACK) {
    if (press) {
      stop_rx();
      ui_switch_screen(SCREEN_SUBGHZ_MENU);
    }
    return;
  }

  if (!s_options) {
    if (s_is_raw && press && ev->button == INPUT_BTN_OK)
      subghz_receiver_finish();
    return;
  }

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav) {
        capture_result_next(&s_cr);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        capture_result_prev(&s_cr);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        switch (capture_result_selected(&s_cr)) {
          case CAP_ACT_PRIMARY:
            send_current();
            break;
          case CAP_ACT_SAVE:
            if (!s_result.save_name[0]) {
              notify(NOTIFY_WARNING, "Capture not saved; check SD");
              break;
            }
            if (!s_saved)
              keyboard_open_filename("Signal name (without .sub)",
                                     s_result.save_name,
                                     sizeof(s_result.save_name) - 1,
                                     save_name_submit);
            break;
          case CAP_ACT_AGAIN:
            ui_subghz_read_open();
            return;
          case CAP_ACT_DISCARD:
            stop_rx();
            ui_switch_screen(SCREEN_SUBGHZ_MENU);
            return;
          default:
            break;
        }
      }
      break;
    default:
      break;
  }
}

void ui_subghz_read_stop(void) {
  stop_rx();
  subghz_receiver_discard();
  if (s_tick_timer != NULL) {
    lv_timer_delete(s_tick_timer);
    s_tick_timer = NULL;
  }
}
