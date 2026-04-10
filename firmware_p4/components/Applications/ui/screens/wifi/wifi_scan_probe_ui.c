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

#include "wifi_scan_probe_ui.h"

#include <stdio.h>

#include "esp_log.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "probe_monitor.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_PROBE_SCAN";

#define TITLE_OFFSET_Y  30
#define LOG_W           230
#define LOG_H           150
#define LOG_OFFSET_Y    10
#define LOG_BORDER_W    1
#define LOG_TAIL_COUNT  15
#define LOG_LINE_MAX    96
#define UPDATE_TIMER_MS 500

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_lbl_count = NULL;
static lv_obj_t *s_ta_log = NULL;
static lv_timer_t *s_update_timer = NULL;
static bool s_is_running = false;
static uint16_t s_last_count = 0;

extern lv_group_t *main_group;

static void update_log(lv_timer_t *t) {
  (void)t;
  if (!s_is_running)
    return;

  uint16_t count = 0;
  probe_monitor_record_t *results = probe_monitor_get_results(&count);

  if (results == NULL)
    return;
  if (count == s_last_count)
    return;

  lv_label_set_text_fmt(s_lbl_count, "Probes: %d", count);

  lv_textarea_set_text(s_ta_log, "");
  int start_idx = (count > LOG_TAIL_COUNT) ? count - LOG_TAIL_COUNT : 0;
  for (int i = start_idx; i < count; i++) {
    char line[LOG_LINE_MAX];
    snprintf(line,
             sizeof(line),
             "[%02X:%02X:%02X:%02X:%02X:%02X] -> \"%s\"\n",
             results[i].mac[0],
             results[i].mac[1],
             results[i].mac[2],
             results[i].mac[3],
             results[i].mac[4],
             results[i].mac[5],
             results[i].ssid);
    lv_textarea_add_text(s_ta_log, line);
  }
  s_last_count = count;
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
      if (s_is_running) {
        probe_monitor_stop();
        s_is_running = false;
      }
      if (s_update_timer != NULL) {
        lv_timer_del(s_update_timer);
        s_update_timer = NULL;
      }
      ui_switch_screen(SCREEN_WIFI_SCAN_MENU);
    }
  }
}

void ui_wifi_scan_probe_open(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  s_lbl_count = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_count, "Probes: 0");
  lv_obj_set_style_text_color(s_lbl_count, current_theme.text_main, 0);
  lv_obj_align(s_lbl_count, LV_ALIGN_TOP_MID, 0, TITLE_OFFSET_Y);

  s_ta_log = lv_textarea_create(s_screen);
  lv_obj_set_size(s_ta_log, LOG_W, LOG_H);
  lv_obj_align(s_ta_log, LV_ALIGN_CENTER, 0, LOG_OFFSET_Y);
  lv_obj_set_style_text_font(s_ta_log, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(s_ta_log, current_theme.screen_base, 0);
  lv_obj_set_style_text_color(s_ta_log, current_theme.border_accent, 0);
  lv_obj_set_style_radius(s_ta_log, 0, 0);
  lv_obj_set_style_border_width(s_ta_log, LOG_BORDER_W, 0);
  lv_obj_set_style_border_color(s_ta_log, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(s_ta_log, LOG_BORDER_W, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_color(s_ta_log, current_theme.border_accent, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(s_ta_log, 0, LV_STATE_FOCUS_KEY);
  lv_textarea_set_text(s_ta_log, "Listening...\n");

  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_ta_log, screen_event_cb, LV_EVENT_KEY, NULL);

  if (!s_is_running) {
    probe_monitor_start();
    s_is_running = true;
    s_last_count = 0;
  }

  if (s_update_timer != NULL)
    lv_timer_del(s_update_timer);
  s_update_timer = lv_timer_create(update_log, UPDATE_TIMER_MS, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_ta_log);
    lv_group_focus_obj(s_ta_log);
  }

  lv_screen_load(s_screen);
}
