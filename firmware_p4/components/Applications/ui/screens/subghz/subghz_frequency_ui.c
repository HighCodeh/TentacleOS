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

#include "subghz_frequency_ui.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "frequency_analyzer.h"
#include "led_control.h"
#include "power_policy.h"
#include "subghz_menu_ui.h"
#include "subghz_settings.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define MARGIN                    8
#define GAP                       6
#define CHIP_H                    24
#define ACTION_H                  26
#define HERO_PORTRAIT_H           92
#define HERO_LANDSCAPE_H          76
#define UPDATE_MS                 40
#define SIGNAL_VISIBLE_MS         500
#define START_WAIT_MS             3000
#define ACTION_COUNT              2
#define ACTION_RESUME             0
#define ACTION_RAW                1
#define DBM_MIN                   (-120)
#define DBM_MAX                   (-25)
#define MHZ                       1000000U
#define KHZ                       1000U
#define BAR_GAP                   2
#define FEEDBACK_EVERY_DETECTIONS 10
#define HERO_PAD                  8
#define HISTORY_ROW_H             15

static const char *TAG = "FREQ_UI";
static lv_obj_t *s_screen, *s_band, *s_threshold_label, *s_status, *s_frequency, *s_rssi;
static lv_obj_t *s_scan, *s_footer, *s_meter, *s_history, *s_actions[ACTION_COUNT];
static lv_obj_t *s_bars[FREQUENCY_ANALYZER_TRACE];
static lv_timer_t *s_timer;
static frequency_analyzer_snapshot_t s_data;
static frequency_analyzer_mode_t s_mode;
static int16_t s_threshold;
static bool s_owns, s_pending, s_paused, s_error, s_open_raw;
static uint32_t s_started_at, s_seen_detections, s_raw_frequency;
static uint32_t s_last_detection, s_signal_at;
static float s_display_dbm;
static unsigned s_selected;
static int32_t s_bar_height;

static void update(lv_timer_t *timer);
static void input(const input_event_t *event, void *context);

static void set_label(lv_obj_t *obj, const char *text) {
  if (strcmp(lv_label_get_text(obj), text))
    lv_label_set_text(obj, text);
}

static void set_label_fmt(lv_obj_t *obj, const char *format, ...) {
  char text[96];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  set_label(obj, text);
}

static void set_error(const char *message) {
  if (!s_error) {
    ESP_LOGE(TAG, "%s", message);
    led_signal_error();
  }
  s_error = true;
  s_pending = false;
  s_open_raw = false;
  set_label(s_status, "RADIO UNAVAILABLE");
  set_label(s_rssi, message);
  ui_chrome_footer_set_text(s_footer, "BACK to Sub-GHz");
  power_policy_set_display_hold(s_screen, false);
}

void ui_subghz_frequency_stop(void) {
  s_pending = false;
  s_open_raw = false;
  if (s_owns)
    frequency_analyzer_stop();
  s_owns = false;
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (s_screen != NULL)
    power_policy_set_display_hold(s_screen, false);
}

static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen) {
    ui_subghz_frequency_stop();
    s_screen = NULL;
  }
}

static lv_obj_t *label(lv_obj_t *parent,
                       int32_t x,
                       int32_t y,
                       int32_t width,
                       const lv_font_t *font,
                       lv_color_t color) {
  lv_obj_t *obj = lv_label_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_width(obj, width);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, color, 0);
  return obj;
}

static lv_obj_t *panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int32_t height) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, width, height);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_radius(obj, 10, 0);
  lv_obj_set_style_bg_color(obj, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_color(obj, current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  return obj;
}

static void refresh_controls(void) {
  set_label_fmt(s_band, "L/R  %s", frequency_analyzer_mode_name(s_mode));
  set_label_fmt(s_threshold_label, "U/D  %d dBm", s_threshold);
  set_label(lv_obj_get_child(s_actions[ACTION_RESUME], 0), s_paused ? "Resume" : "Pause");
  for (unsigned i = 0; i < ACTION_COUNT; i++) {
    bool is_selected = s_paused ? i == s_selected : i == ACTION_RESUME;
    lv_obj_set_style_border_color(
        s_actions[i], is_selected ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_bg_color(
        s_actions[i],
        is_selected
            ? lv_color_mix(current_theme.border_accent, current_theme.bg_secondary, LV_OPA_20)
            : current_theme.bg_secondary,
        0);
    lv_obj_set_style_opa(
        s_actions[i], i == ACTION_RAW && !s_data.frequency_hz ? LV_OPA_40 : LV_OPA_COVER, 0);
  }
  ui_chrome_footer_set_text(s_footer,
                            s_paused ? "L/R choose   OK do   BACK exit" : "OK pause   BACK exit");
}

static void pause_toggle(void) {
  if (!s_owns || s_error || s_open_raw)
    return;
  s_paused = !s_paused;
  s_selected = ACTION_RESUME;
  frequency_analyzer_pause(s_paused);
  refresh_controls();
}

static void open_raw(void) {
  if (!s_owns || !s_data.frequency_hz || s_error || s_open_raw)
    return;
  s_raw_frequency = s_data.frequency_hz;
  s_open_raw = true;
  s_started_at = lv_tick_get();
  frequency_analyzer_stop();
  set_label(s_status, "OPENING RAW READER");
}

static void clicked(lv_event_t *event) {
  if (lv_event_get_target(event) == s_actions[ACTION_RAW])
    open_raw();
  else
    pause_toggle();
}

static void update(lv_timer_t *timer) {
  (void)timer;
  if (s_pending) {
    esp_err_t error = frequency_analyzer_start(s_mode, s_threshold);
    if (error == ESP_OK) {
      s_pending = false;
      s_owns = true;
    } else if (error != ESP_ERR_INVALID_STATE || lv_tick_elaps(s_started_at) >= START_WAIT_MS)
      set_error(error == ESP_ERR_INVALID_STATE ? "Radio busy. Close other RF tasks."
                                               : "Unable to start receiver");
    if (!s_owns)
      return;
  }
  if (s_open_raw) {
    if (!frequency_analyzer_is_running()) {
      s_owns = false;
      s_open_raw = false;
      subghz_settings_set_freq(s_raw_frequency);
      ui_subghz_read_set_raw(true);
      ui_switch_screen(SCREEN_SUBGHZ_READ);
    } else if (lv_tick_elaps(s_started_at) >= START_WAIT_MS)
      set_error("Radio cleanup failed");
    return;
  }
  if (!s_owns || s_error)
    return;
  frequency_analyzer_get_snapshot(&s_data);
  if (s_data.error != ESP_OK) {
    set_error("Radio read failed. BACK to exit.");
    return;
  }
  if (s_data.detections != s_last_detection) {
    s_last_detection = s_data.detections;
    s_signal_at = lv_tick_get();
  }
  if (s_data.frequency_hz) {
    set_label_fmt(s_frequency,
                  "%lu.%03lu MHz",
                  (unsigned long)(s_data.frequency_hz / MHZ),
                  (unsigned long)((s_data.frequency_hz % MHZ) / KHZ));
    char strength[64];
    snprintf(strength, sizeof(strength), "%.1f dBm | 20 kHz scan step", (double)s_data.rssi_dbm);
    set_label(s_rssi, strength);
  } else {
    set_label(s_frequency, "---.--- MHz");
    set_label(s_rssi, "Hold the remote button nearby");
  }
  const char *status = s_data.frequency_hz ? "LAST FOUND / SCANNING" : "SEARCHING";
  if (s_data.phase == FREQUENCY_ANALYZER_REFINING)
    status = "REFINING FREQUENCY";
  if (s_data.phase == FREQUENCY_ANALYZER_FOUND)
    status = "SIGNAL FOUND";
  if (s_data.frequency_hz && s_last_detection && lv_tick_elaps(s_signal_at) < SIGNAL_VISIBLE_MS)
    status = "SIGNAL FOUND";
  if (s_paused)
    status = s_data.phase == FREQUENCY_ANALYZER_PAUSED ? "PAUSED" : "PAUSING...";
  set_label(s_status, status);
  float dbm = s_paused && s_data.frequency_hz ? s_data.rssi_dbm : s_data.live_dbm;
  s_display_dbm += (dbm - s_display_dbm) * 0.35f;
  lv_bar_set_value(s_meter, (int32_t)s_display_dbm, LV_ANIM_OFF);
  char scan[64];
  snprintf(scan,
           sizeof(scan),
           "%s %lu.%03lu  |  %.0f dBm",
           s_paused ? "Last" : "Scan",
           (unsigned long)(s_data.scan_frequency_hz / MHZ),
           (unsigned long)((s_data.scan_frequency_hz % MHZ) / KHZ),
           (double)s_data.live_dbm);
  set_label(s_scan, scan);
  for (unsigned i = 0; i < FREQUENCY_ANALYZER_TRACE; i++) {
    float ratio = i < s_data.trace_count ? (s_data.trace[i] - DBM_MIN) / (DBM_MAX - DBM_MIN) : 0;
    if (ratio < 0)
      ratio = 0;
    if (ratio > 1)
      ratio = 1;
    int32_t height = 1 + (int32_t)(ratio * (s_bar_height - 1));
    lv_obj_set_height(s_bars[i], height);
    lv_obj_set_style_bg_color(s_bars[i],
                              i < s_data.trace_count && s_data.trace[i] >= s_threshold
                                  ? current_theme.border_accent
                                  : current_theme.border_inactive,
                              0);
  }
  if (s_history != NULL) {
    char history[150] = "";
    size_t used = 0;
    for (unsigned i = 0; i < FREQUENCY_ANALYZER_HISTORY; i++) {
      frequency_analyzer_hit_t hit = s_data.history[i];
      int written =
          snprintf(history + used,
                   sizeof(history) - used,
                   hit.frequency_hz ? "%s%lu.%03lu MHz    %.1f dBm" : "%s---.--- MHz    -- dBm",
                   i ? "\n" : "",
                   (unsigned long)(hit.frequency_hz / MHZ),
                   (unsigned long)((hit.frequency_hz % MHZ) / KHZ),
                   (double)hit.rssi_dbm);
      if (written < 0 || (size_t)written >= sizeof(history) - used)
        break;
      used += (size_t)written;
    }
    set_label(s_history, history);
  }
  if (s_data.detections != s_seen_detections) {
    if (!s_seen_detections || s_data.detections - s_seen_detections >= FEEDBACK_EVERY_DETECTIONS) {
      ESP_LOGI(TAG,
               "Found %lu Hz at %.1f dBm",
               (unsigned long)s_data.frequency_hz,
               (double)s_data.rssi_dbm);
      led_signal_info();
      s_seen_detections = s_data.detections;
    }
  }
  refresh_controls();
}

static void input(const input_event_t *event, void *context) {
  (void)context;
  bool is_press = event->action == INPUT_ACTION_PRESS;
  bool is_nav = is_press || event->action == INPUT_ACTION_REPEAT;
  if (is_press && event->button == INPUT_BTN_BACK) {
    ui_switch_screen(SCREEN_SUBGHZ_MENU);
    return;
  }
  if (s_error || s_open_raw || s_pending)
    return;
  if (is_press && event->button == INPUT_BTN_OK) {
    if (s_paused && s_selected == ACTION_RAW)
      open_raw();
    else
      pause_toggle();
  } else if (is_nav && (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT)) {
    if (s_paused)
      s_selected = (s_selected + 1) % ACTION_COUNT;
    else {
      int direction = event->button == INPUT_BTN_RIGHT ? 1 : -1;
      s_mode = (s_mode + FREQUENCY_ANALYZER_MODE_COUNT + direction) % FREQUENCY_ANALYZER_MODE_COUNT;
      frequency_analyzer_configure(s_mode, s_threshold);
      s_data.frequency_hz = 0;
    }
  } else if (is_nav && !s_paused &&
             (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN)) {
    int next = s_threshold + (event->button == INPUT_BTN_UP ? FREQUENCY_ANALYZER_THRESHOLD_STEP
                                                            : -FREQUENCY_ANALYZER_THRESHOLD_STEP);
    if (next >= FREQUENCY_ANALYZER_THRESHOLD_MIN && next <= FREQUENCY_ANALYZER_THRESHOLD_MAX) {
      s_threshold = next;
      frequency_analyzer_configure(s_mode, s_threshold);
    }
  } else
    return;
  ui_feedback(UI_FB_NAV);
  refresh_controls();
}

void ui_subghz_frequency_open(void) {
  ui_subghz_frequency_stop();
  if (s_screen != NULL)
    lv_obj_delete(s_screen);
  s_screen = ui_chrome_screen_create();
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  s_mode = FREQUENCY_ANALYZER_COMMON;
  s_threshold = FREQUENCY_ANALYZER_THRESHOLD_DEFAULT;
  s_paused = false;
  s_error = false;
  s_open_raw = false;
  s_selected = 0;
  s_seen_detections = 0;
  s_last_detection = 0;
  s_signal_at = 0;
  s_display_dbm = DBM_MIN;
  s_data = (frequency_analyzer_snapshot_t){0};
  ui_chrome_header(s_screen, "FREQUENCY", "/assets/icons/graphic_eq.bin");
  int32_t width = ui_screen_w() - 2 * MARGIN;
  bool is_wide = ui_screen_w() > ui_screen_h();
  int32_t top = UI_CHROME_HEADER_H + GAP;
  int32_t chip_width = (width - GAP) / 2;
  lv_obj_t *chip = panel(s_screen, MARGIN, top, chip_width, CHIP_H);
  s_band = label(chip,
                 HERO_PAD,
                 4,
                 chip_width - 2 * HERO_PAD,
                 &lv_font_montserrat_12,
                 current_theme.text_main);
  chip = panel(s_screen, MARGIN + chip_width + GAP, top, chip_width, CHIP_H);
  s_threshold_label = label(chip,
                            HERO_PAD,
                            4,
                            chip_width - 2 * HERO_PAD,
                            &lv_font_montserrat_12,
                            current_theme.text_secondary);
  int32_t hero_height = is_wide ? HERO_LANDSCAPE_H : HERO_PORTRAIT_H;
  top += CHIP_H + GAP;
  lv_obj_t *hero = panel(s_screen, MARGIN, top, width, hero_height);
  lv_obj_set_style_border_color(hero, current_theme.border_accent, 0);
  s_status = label(
      hero, HERO_PAD, 5, width - 2 * HERO_PAD, &lv_font_montserrat_12, current_theme.border_accent);
  s_frequency = label(hero,
                      HERO_PAD,
                      is_wide ? 20 : 25,
                      width - 2 * HERO_PAD,
                      &lv_font_montserrat_24,
                      current_theme.text_main);
  s_rssi = label(hero,
                 HERO_PAD,
                 hero_height - 25,
                 width - 2 * HERO_PAD,
                 &lv_font_montserrat_12,
                 current_theme.text_secondary);
  s_meter = lv_bar_create(hero);
  lv_obj_set_pos(s_meter, HERO_PAD, hero_height - 8);
  lv_obj_set_size(s_meter, width - 2 * HERO_PAD, 3);
  lv_bar_set_range(s_meter, DBM_MIN, DBM_MAX);
  lv_bar_set_value(s_meter, DBM_MIN, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_meter, current_theme.border_inactive, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_meter, current_theme.border_accent, LV_PART_INDICATOR);
  top += hero_height + GAP;
  s_scan =
      label(s_screen, MARGIN, top, width, &lv_font_montserrat_12, current_theme.text_secondary);
  int32_t actions_y = ui_screen_h() - UI_CHROME_FOOTER_H - GAP - ACTION_H;
  s_bar_height = is_wide ? 8 : 23;
  int32_t plot_y = top + 15;
  int32_t stride = width / FREQUENCY_ANALYZER_TRACE;
  for (unsigned i = 0; i < FREQUENCY_ANALYZER_TRACE; i++) {
    lv_obj_t *bar = lv_obj_create(s_screen);
    s_bars[i] = bar;
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, stride - BAR_GAP, 1);
    lv_obj_set_align(bar, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_pos(bar, MARGIN + i * stride, plot_y + s_bar_height - ui_screen_h());
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, current_theme.border_inactive, 0);
  }
  s_history = NULL;
  if (!is_wide) {
    s_history = label(s_screen,
                      MARGIN,
                      plot_y + s_bar_height + GAP,
                      width,
                      &lv_font_montserrat_12,
                      current_theme.text_secondary);
    lv_obj_set_height(s_history, FREQUENCY_ANALYZER_HISTORY * HISTORY_ROW_H);
    lv_obj_set_style_text_line_space(s_history, 0, 0);
  }
  for (unsigned i = 0; i < ACTION_COUNT; i++) {
    s_actions[i] =
        panel(s_screen, MARGIN + i * (chip_width + GAP), actions_y, chip_width, ACTION_H);
    lv_obj_t *text =
        label(s_actions[i], 0, 0, chip_width - 2, &lv_font_montserrat_12, current_theme.text_main);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(text);
    set_label(text, i == ACTION_RAW ? "Read RAW" : "Pause");
    lv_obj_add_event_cb(s_actions[i], clicked, LV_EVENT_CLICKED, NULL);
  }
  s_footer = ui_chrome_footer(s_screen, "OK pause   BACK exit");
  set_label(s_status, "STARTING RECEIVER");
  set_label(s_frequency, "---.--- MHz");
  set_label(s_rssi, "Hold the remote button nearby");
  set_label(s_scan, "Auto: common channels / Full: all bands");
  refresh_controls();
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  s_started_at = lv_tick_get();
  s_pending = true;
  s_timer = lv_timer_create(update, UPDATE_MS, NULL);
  if (s_timer == NULL) {
    set_error("Not enough memory");
    return;
  }
  power_policy_set_display_hold(s_screen, true);
}
