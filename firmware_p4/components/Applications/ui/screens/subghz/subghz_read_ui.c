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
#include <inttypes.h>

#include "esp_log.h"
#include "lvgl.h"
#include "cc1101_rf_path.h"
#include "keyboard_ui.h"
#include "led_control.h"
#include "notify_ui.h"
#include "subghz_receiver.h"
#include "subghz_replay.h"
#include "subghz_settings.h"
#include "subghz_storage.h"
#include "subghz_transmitter.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "power_policy.h"

#define UPDATE_MS        50
#define START_WAIT_MS    3000
#define MARGIN           8
#define GAP              6
#define ACTION_H         26
#define SUMMARY_H        30
#define GRAPH_Y          116
#define GRAPH_PAD        4
#define HISTORY_COUNT    48
#define PLOT_POINT_COUNT (SUBGHZ_RX_PREVIEW_PULSES * 2)
#define RSSI_MIN_DBM     (-110.0f)
#define RSSI_MAX_DBM     (-20.0f)
#define ACTION_COUNT     3
#define ACTION_SAVE      0
#define ACTION_SEND      1
#define ACTION_AGAIN     2
#define MHZ              1000000U

static const char *TAG = "SUBGHZ_RD";
static const char *const ACTION_LABELS[ACTION_COUNT] = {"Save", "Send", "Again"};
static lv_obj_t *s_screen, *s_frequency, *s_meta, *s_rssi, *s_peak, *s_bar;
static lv_obj_t *s_graph, *s_wave, *s_summary, *s_hint, *s_actions[ACTION_COUNT];
static lv_timer_t *s_timer;
static lv_point_precise_t s_points[PLOT_POINT_COUNT];
static float s_history[HISTORY_COUNT];
static uint16_t s_history_count;
static uint32_t s_rssi_seq, s_opened_at;
static bool s_raw, s_locked, s_owns_rx, s_sending, s_pending, s_error;
static int s_selected;
static subghz_rx_result_t s_result;
static subghz_settings_t s_config;
static char s_frequency_text[24];

static void format_frequency(uint32_t hz) {
  if (hz == 0)
    snprintf(s_frequency_text, sizeof(s_frequency_text), "Frequency hopping");
  else
    snprintf(s_frequency_text,
             sizeof(s_frequency_text),
             "%lu.%03lu MHz",
             (unsigned long)(hz / MHZ),
             (unsigned long)((hz % MHZ) / 1000));
  lv_label_set_text(s_frequency, s_frequency_text);
}

static const char *preset_name(cc1101_preset_t preset) {
  switch (preset) {
    case CC1101_PRESET_OOK_270KHZ:
      return "AM270";
    case CC1101_PRESET_OOK_650KHZ:
      return "AM650";
    case CC1101_PRESET_OOK_800KHZ:
      return "AM812";
    case CC1101_PRESET_2FSK_2KHZ:
      return "FM2.38";
    case CC1101_PRESET_2FSK_47KHZ:
      return "FM47.6";
    case CC1101_PRESET_2FSK_95KHZ:
      return "FM95.2";
    default:
      return "Unknown";
  }
}

static void radio_meta(uint32_t hz) {
  lv_label_set_text_fmt(s_meta,
                        "%s  RF%u  |  TX +10 dBm",
                        preset_name(s_config.preset),
                        (unsigned)cc1101_rf_path_for_frequency(hz));
}

static void stop_rx(void) {
  if (s_owns_rx)
    subghz_receiver_stop();
  s_owns_rx = false;
}

void ui_subghz_read_stop(void) {
  s_pending = false;
  stop_rx();
  if (s_sending)
    subghz_tx_stop();
  s_sending = false;
  if (s_timer) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (s_screen)
    power_policy_set_display_hold(s_screen, false);
}

static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen)
    ui_subghz_read_stop();
}

void ui_subghz_read_set_raw(bool is_raw) {
  s_raw = is_raw;
}

static void select_action(void) {
  for (int i = 0; i < ACTION_COUNT; i++) {
    lv_obj_set_style_bg_color(
        s_actions[i],
        i == s_selected
            ? lv_color_mix(current_theme.border_accent, current_theme.bg_secondary, LV_OPA_30)
            : current_theme.bg_secondary,
        0);
    lv_obj_set_style_border_color(s_actions[i],
                                  i == s_selected ? current_theme.border_accent
                                                  : current_theme.border_inactive,
                                  0);
  }
}

static int32_t graph_width(void) {
  return ui_screen_w() - 2 * MARGIN - 2 * GRAPH_PAD - 3;
}
static int32_t graph_height(void) {
  return lv_obj_get_height(s_graph) - 2 * GRAPH_PAD - 3;
}

static void plot_history(void) {
  int32_t width = graph_width(), height = graph_height();
  for (unsigned i = 0; i < s_history_count; i++) {
    float level = (s_history[i] - RSSI_MIN_DBM) / (RSSI_MAX_DBM - RSSI_MIN_DBM);
    if (level < 0)
      level = 0;
    if (level > 1)
      level = 1;
    s_points[i].x = i * width / (HISTORY_COUNT - 1);
    s_points[i].y = (int32_t)((1 - level) * height);
  }
  if (s_history_count >= 2)
    lv_line_set_points(s_wave, s_points, s_history_count);
}

static void plot_capture(void) {
  uint64_t duration = 0, elapsed = 0;
  for (unsigned i = 0; i < s_result.preview_count; i++)
    duration += s_result.preview[i] < 0 ? -(int64_t)s_result.preview[i] : s_result.preview[i];
  if (!duration)
    return;
  int32_t width = graph_width(), height = graph_height();
  for (unsigned i = 0; i < s_result.preview_count; i++) {
    int32_t pulse = s_result.preview[i];
    int32_t level = pulse > 0 ? 2 : height - 2;
    s_points[i * 2] = (lv_point_precise_t){elapsed * width / duration, level};
    elapsed += pulse < 0 ? -(int64_t)pulse : pulse;
    s_points[i * 2 + 1] = (lv_point_precise_t){elapsed * width / duration, level};
  }
  lv_line_set_points(s_wave, s_points, s_result.preview_count * 2);
}

static void capture_ready(void) {
  stop_rx();
  s_locked = true;
  format_frequency(s_result.freq);
  radio_meta(s_result.freq);
  plot_capture();
  char summary[160];
  if (s_result.decoded)
    snprintf(summary,
             sizeof(summary),
             "%s  %u bit  0x%" PRIX32 "\nRAW %u/%" PRIu32 " pulses  |  %.1f ms",
             s_result.name_buf,
             s_result.data.bit_count,
             s_result.data.raw_value,
             s_result.preview_count,
             s_result.pulse_count,
             s_result.duration_us / 1000.0);
  else
    snprintf(summary,
             sizeof(summary),
             "%s  |  %" PRIu32 " pulses  %.1f ms\nPreview: first %u pulses%s",
             s_raw ? "RAW captured" : "Unknown signal",
             s_result.pulse_count,
             s_result.duration_us / 1000.0,
             s_result.preview_count,
             s_result.save_name[0] ? "" : " / SD failed");
  lv_label_set_text(s_summary, summary);
  for (int i = 0; i < ACTION_COUNT; i++)
    lv_obj_remove_flag(s_actions[i], LV_OBJ_FLAG_HIDDEN);
  s_selected = ACTION_SAVE;
  select_action();
  ui_chrome_footer_set_text(s_hint, "L/R choose  OK confirm  BACK exit");
  ESP_LOGI(TAG,
           "%s captured on %lu Hz: %lu pulses, SD=%s",
           s_raw ? "RAW" : "Read",
           (unsigned long)s_result.freq,
           (unsigned long)s_result.pulse_count,
           s_result.save_name[0] ? s_result.save_name : "failed");
  ui_feedback(UI_FB_READ);
}

static void receive_error(const char *message) {
  s_error = true;
  s_pending = false;
  stop_rx();
  lv_label_set_text(s_summary, message);
  ui_chrome_footer_set_text(s_hint, "OK retry  BACK exit");
  ESP_LOGE(TAG, "%s", message);
  led_signal_error();
}

static void update(lv_timer_t *timer) {
  (void)timer;
  if (lv_screen_active() != s_screen)
    return;
  if (s_pending) {
    if (subghz_receiver_is_running()) {
      if (lv_tick_elaps(s_opened_at) > START_WAIT_MS)
        receive_error("Radio busy. Try again.");
      return;
    }
    s_pending = false;
    esp_err_t error = subghz_receiver_start(
        s_raw ? SUBGHZ_MODE_RAW : SUBGHZ_MODE_SCAN, s_config.preset, s_config.freq);
    s_owns_rx = error == ESP_OK;
    if (error != ESP_OK) {
      receive_error("Cannot start reception.");
      return;
    }
    lv_label_set_text(s_summary,
                      s_raw ? "Listening for RAW pulses...\nPress your remote."
                            : "Listening for a remote...\nGraph: live RSSI history");
  }
  if (s_sending) {
    subghz_tx_status_t tx;
    subghz_tx_get_status(&tx);
    if (!tx.is_busy || tx.error != ESP_OK) {
      s_sending = false;
      subghz_tx_stop();
      notify(tx.error == ESP_OK ? NOTIFY_INFO : NOTIFY_WARNING,
             tx.error == ESP_OK ? "Transmission finished" : "Transmission failed");
    }
  }
  if (s_error || s_locked)
    return;
  if (subghz_receiver_get_error() != ESP_OK) {
    receive_error("Reception failed. Try again.");
    return;
  }
  subghz_rx_monitor_t monitor;
  if (subghz_receiver_get_monitor(&monitor) && monitor.rssi_valid &&
      monitor.sample_seq != s_rssi_seq) {
    s_rssi_seq = monitor.sample_seq;
    format_frequency(monitor.frequency_hz);
    radio_meta(monitor.frequency_hz);
    char reading[32];
    snprintf(reading, sizeof(reading), "RSSI %.1f dBm", (double)monitor.rssi_dbm);
    lv_label_set_text(s_rssi, reading);
    snprintf(reading, sizeof(reading), "Peak %.1f", (double)monitor.peak_dbm);
    lv_label_set_text(s_peak, reading);
    lv_bar_set_value(s_bar, (int32_t)monitor.rssi_dbm, LV_ANIM_OFF);
    if (s_history_count == HISTORY_COUNT) {
      memmove(s_history, s_history + 1, sizeof(s_history) - sizeof(s_history[0]));
      s_history_count--;
    }
    s_history[s_history_count++] = monitor.rssi_dbm;
    plot_history();
  }
  if (subghz_receiver_get_result(&s_result))
    capture_ready();
}

static void saved_name(const char *name, void *context) {
  (void)context;
  esp_err_t error = subghz_storage_rename(s_result.save_name, name);
  if (error == ESP_OK) {
    snprintf(s_result.save_name, sizeof(s_result.save_name), "%s", name);
    ui_feedback(UI_FB_WRITE);
    notify(NOTIFY_SAVED, "Saved to SD");
  } else
    notify(NOTIFY_WARNING, "Could not save: check name / SD");
}

static void input(const input_event_t *event, void *context) {
  (void)context;
  bool press = event->action == INPUT_ACTION_PRESS;
  bool nav = press || event->action == INPUT_ACTION_REPEAT;
  if (press && event->button == INPUT_BTN_BACK) {
    ui_switch_screen(SCREEN_SUBGHZ_MENU);
    return;
  }
  if (s_error && press && event->button == INPUT_BTN_OK) {
    ui_subghz_read_open();
    return;
  }
  if (!s_locked || s_sending)
    return;
  if (nav && (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_UP))
    s_selected = (s_selected + ACTION_COUNT - 1) % ACTION_COUNT;
  else if (nav && (event->button == INPUT_BTN_RIGHT || event->button == INPUT_BTN_DOWN))
    s_selected = (s_selected + 1) % ACTION_COUNT;
  else if (press && event->button == INPUT_BTN_OK) {
    if (s_selected == ACTION_AGAIN) {
      ui_subghz_read_open();
      return;
    }
    if (!s_result.save_name[0]) {
      notify(NOTIFY_WARNING, "Capture not saved; check SD");
      return;
    }
    if (s_selected == ACTION_SAVE) {
      keyboard_open_filename("Signal name (without .sub)",
                             s_result.save_name,
                             sizeof(s_result.save_name) - 1,
                             saved_name);
    } else {
      esp_err_t error = subghz_replay_file(s_result.save_name);
      if (error == ESP_OK) {
        s_sending = true;
        notify(NOTIFY_INFO, "Transmitting at +10 dBm nominal");
      } else
        notify(NOTIFY_WARNING, "Cannot send this capture");
    }
  }
  select_action();
}

static lv_obj_t *make_label(int32_t y, int32_t height) {
  lv_obj_t *label = lv_label_create(s_screen);
  if (!label)
    return NULL;
  lv_obj_set_pos(label, MARGIN, y);
  lv_obj_set_size(label, ui_screen_w() - MARGIN * 2, height);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, current_theme.text_main, 0);
  return label;
}

void ui_subghz_read_open(void) {
  ui_subghz_read_stop();
  if (s_screen) {
    lv_obj_delete(s_screen);
    s_screen = NULL;
  }
  s_frequency = s_meta = s_rssi = s_peak = s_bar = NULL;
  s_graph = s_wave = s_summary = s_hint = NULL;
  memset(s_actions, 0, sizeof(s_actions));
  s_locked = s_error = s_sending = false;
  s_result = (subghz_rx_result_t){0};
  s_history_count = 0;
  s_rssi_seq = 0;
  s_config = subghz_settings_get();
  s_screen = ui_chrome_screen_create();
  if (!s_screen)
    return;
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  ui_chrome_header(s_screen, s_raw ? "READ RAW" : "READ", "/assets/icons/sensors.bin");
  s_hint = ui_chrome_footer(s_screen, "BACK exit");
  ui_input_set_screen_handler(input, NULL);
  s_frequency = make_label(48, 20);
  s_meta = make_label(70, 16);
  s_rssi = make_label(90, 16);
  s_peak = make_label(90, 16);
  if (!s_frequency || !s_meta || !s_rssi || !s_peak || !s_hint)
    goto failed;
  lv_obj_set_style_text_font(s_frequency, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(s_frequency, current_theme.border_accent, 0);
  lv_obj_set_style_text_color(s_meta, current_theme.text_secondary, 0);
  lv_obj_set_width(s_rssi, (ui_screen_w() - MARGIN * 2) * 3 / 5);
  lv_obj_set_pos(s_peak, ui_screen_w() / 2 + 20, 90);
  lv_obj_set_width(s_peak, ui_screen_w() / 2 - 20 - MARGIN);
  lv_obj_set_style_text_align(s_peak, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(s_rssi, "RSSI -- dBm");
  lv_label_set_text(s_peak, "Peak --");
  format_frequency(s_config.freq);
  radio_meta(s_config.freq);
  s_bar = lv_bar_create(s_screen);
  if (!s_bar)
    goto failed;
  lv_obj_set_pos(s_bar, MARGIN, 108);
  lv_obj_set_size(s_bar, ui_screen_w() - MARGIN * 2, 4);
  lv_bar_set_range(s_bar, RSSI_MIN_DBM, RSSI_MAX_DBM);
  lv_bar_set_value(s_bar, RSSI_MIN_DBM, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_bar, current_theme.bg_secondary, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_bar, current_theme.border_accent, LV_PART_INDICATOR);
  int32_t action_y = ui_screen_h() - UI_CHROME_FOOTER_H - GAP - ACTION_H;
  int32_t summary_y = action_y - GAP - SUMMARY_H;
  s_graph = lv_obj_create(s_screen);
  if (!s_graph)
    goto failed;
  lv_obj_set_pos(s_graph, MARGIN, GRAPH_Y);
  lv_obj_set_size(s_graph, ui_screen_w() - MARGIN * 2, summary_y - GAP - GRAPH_Y);
  lv_obj_remove_flag(s_graph, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_graph, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_color(s_graph, current_theme.border_interface, 0);
  lv_obj_set_style_border_width(s_graph, 1, 0);
  lv_obj_set_style_radius(s_graph, GAP, 0);
  lv_obj_set_style_pad_all(s_graph, GRAPH_PAD, 0);
  s_wave = lv_line_create(s_graph);
  if (!s_wave)
    goto failed;
  lv_obj_set_style_line_color(s_wave, current_theme.border_accent, 0);
  lv_obj_set_style_line_width(s_wave, 2, 0);
  s_summary = make_label(summary_y, SUMMARY_H);
  if (!s_summary)
    goto failed;
  lv_label_set_text(s_summary, "Preparing receiver...");
  for (int i = 0; i < ACTION_COUNT; i++) {
    s_actions[i] = lv_obj_create(s_screen);
    if (!s_actions[i])
      goto failed;
    int32_t width = (ui_screen_w() - 2 * MARGIN - GAP * (ACTION_COUNT - 1)) / ACTION_COUNT;
    lv_obj_set_pos(s_actions[i], MARGIN + i * (width + GAP), action_y);
    lv_obj_set_size(s_actions[i], width, ACTION_H);
    lv_obj_remove_flag(s_actions[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_actions[i], GAP, 0);
    lv_obj_set_style_pad_all(s_actions[i], 0, 0);
    lv_obj_set_style_border_width(s_actions[i], 1, 0);
    lv_obj_t *label = lv_label_create(s_actions[i]);
    if (!label)
      goto failed;
    lv_label_set_text(label, ACTION_LABELS[i]);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, current_theme.text_main, 0);
    lv_obj_center(label);
    lv_obj_add_flag(s_actions[i], LV_OBJ_FLAG_HIDDEN);
  }
  s_timer = lv_timer_create(update, UPDATE_MS, NULL);
  if (!s_timer)
    goto failed;
  s_pending = true;
  s_opened_at = lv_tick_get();
  power_policy_set_display_hold(s_screen, true);
  ui_screen_load_owned(&s_screen, s_screen);
  return;
failed:
  s_error = true;
  s_pending = false;
  if (s_frequency)
    lv_label_set_text(s_frequency, "Screen allocation failed");
  ESP_LOGE(TAG, "Screen allocation failed");
  led_signal_error();
  ui_screen_load_owned(&s_screen, s_screen);
}
