// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "wifi_probe_ui.h"

#include <stdio.h>

#include "esp_log.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "probe_monitor.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_PROBE";

#define TITLE_OFFSET_Y  30
#define COUNT_OFFSET_X  (-10)
#define COUNT_OFFSET_Y  30
#define LOG_W           220
#define LOG_H           100
#define LOG_OFFSET_Y    (-10)
#define BTN_WIDTH       140
#define BTN_HEIGHT      30
#define BTN_OFFSET_Y    (-35)
#define UPDATE_TIMER_MS 500
#define LOG_TAIL_COUNT  10
#define LOG_LINE_MAX    64

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_btn_toggle = NULL;
static lv_obj_t *s_lbl_count = NULL;
static lv_obj_t *s_ta_log = NULL;
static lv_timer_t *s_update_timer = NULL;
static bool s_is_running = false;
static uint16_t s_last_count = 0;

extern lv_group_t *main_group;

static void update_log(lv_timer_t *t) {
  if (!s_is_running)
    return;

  uint16_t count = 0;
  probe_monitor_record_t *results = probe_monitor_get_results(&count);

  if (count != s_last_count) {
    lv_label_set_text_fmt(s_lbl_count, "Devices: %d", count);

    lv_textarea_set_text(s_ta_log, "");

    int start_idx = (count > LOG_TAIL_COUNT) ? count - LOG_TAIL_COUNT : 0;
    for (int i = start_idx; i < count; i++) {
      char line[LOG_LINE_MAX];
      snprintf(line, sizeof(line), "[%d] %s (%ddBm)\n", i, results[i].ssid, results[i].rssi);
      lv_textarea_add_text(s_ta_log, line);
    }
    s_last_count = count;
  }
}

static void toggle_handler(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER) {
    if (s_is_running) {
      probe_monitor_stop();
      s_is_running = false;
      lv_label_set_text(lv_obj_get_child(s_btn_toggle, 0), "START MONITOR");
      lv_obj_set_style_bg_color(s_btn_toggle, current_theme.bg_item_top, 0);
      if (s_update_timer != NULL)
        lv_timer_pause(s_update_timer);
    } else {
      probe_monitor_start();
      s_is_running = true;
      s_last_count = 0;
      lv_label_set_text(lv_obj_get_child(s_btn_toggle, 0), "STOP MONITOR");
      lv_obj_set_style_bg_color(s_btn_toggle, current_theme.bg_item_bot, 0);
      lv_textarea_set_text(s_ta_log, "Listening...\n");
      if (s_update_timer != NULL)
        lv_timer_resume(s_update_timer);
    }
  }
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_KEY) {
    if (lv_event_get_key(e) == LV_KEY_ESC) {
      if (s_is_running) {
        probe_monitor_stop();
        s_is_running = false;
      }
      if (s_update_timer != NULL) {
        lv_timer_del(s_update_timer);
        s_update_timer = NULL;
      }
      ui_switch_screen(SCREEN_WIFI_MENU);
    }
  }
}

void ui_wifi_probe_open(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  lv_obj_t *title = lv_label_create(s_screen);
  lv_label_set_text(title, "PROBE MONITOR");
  lv_obj_set_style_text_color(title, current_theme.text_main, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, TITLE_OFFSET_Y);

  s_lbl_count = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_count, "Devices: 0");
  lv_obj_set_style_text_color(s_lbl_count, current_theme.text_main, 0);
  lv_obj_align(s_lbl_count, LV_ALIGN_TOP_RIGHT, COUNT_OFFSET_X, COUNT_OFFSET_Y);

  s_ta_log = lv_textarea_create(s_screen);
  lv_obj_set_size(s_ta_log, LOG_W, LOG_H);
  lv_obj_align(s_ta_log, LV_ALIGN_CENTER, 0, LOG_OFFSET_Y);
  lv_obj_set_style_text_font(s_ta_log, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(s_ta_log, current_theme.screen_base, 0);
  lv_obj_set_style_text_color(s_ta_log, current_theme.border_accent, 0);
  lv_textarea_set_text(s_ta_log, "Ready.");

  s_btn_toggle = lv_btn_create(s_screen);
  lv_obj_set_size(s_btn_toggle, BTN_WIDTH, BTN_HEIGHT);
  lv_obj_align(s_btn_toggle, LV_ALIGN_BOTTOM_MID, 0, BTN_OFFSET_Y);
  lv_obj_set_style_bg_color(s_btn_toggle, current_theme.bg_item_top, 0);

  lv_obj_t *lbl_btn = lv_label_create(s_btn_toggle);
  lv_label_set_text(lbl_btn, "START MONITOR");
  lv_obj_center(lbl_btn);

  lv_obj_add_event_cb(s_btn_toggle, toggle_handler, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);

  s_update_timer = lv_timer_create(update_log, UPDATE_TIMER_MS, NULL);
  lv_timer_pause(s_update_timer);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_btn_toggle);
    lv_group_focus_obj(s_btn_toggle);
  }

  lv_screen_load(s_screen);
}
