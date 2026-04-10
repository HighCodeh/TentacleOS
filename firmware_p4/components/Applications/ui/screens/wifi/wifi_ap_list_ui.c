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

#include "wifi_ap_list_ui.h"

#include <stdio.h>

#include "esp_log.h"

#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "ap_scanner.h"
#include "wifi_deauth_ui.h"
#include "wifi_evil_twin_ui.h"

static const char *TAG = "WIFI_AP_LIST_UI";

#define LIST_W            220
#define LIST_H            150
#define LIST_OFFSET_Y     10
#define TITLE_OFFSET_Y    30
#define AP_LABEL_BUF_SIZE 64

extern lv_group_t *main_group;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list = NULL;
static wifi_ap_record_t *s_results = NULL;
static uint16_t s_result_count = 0;

static void on_ap_key_event(lv_event_t *e);
static void on_screen_key_event(lv_event_t *e);

static void on_ap_key_event(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  if (lv_event_get_key(e) != LV_KEY_ENTER)
    return;

  wifi_ap_record_t *ap = (wifi_ap_record_t *)lv_event_get_user_data(e);
  ESP_LOGI(TAG, "Selected AP: %s", ap->ssid);
  ui_wifi_deauth_set_target(ap);
  ui_switch_screen(SCREEN_WIFI_DEAUTH);
}

static void on_screen_key_event(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  if (lv_event_get_key(e) == LV_KEY_ESC)
    ui_switch_screen(SCREEN_WIFI_MENU);
}

void ui_wifi_ap_list_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  lv_obj_t *title = lv_label_create(s_screen);
  lv_label_set_text(title, "Select Target");
  lv_obj_set_style_text_color(title, current_theme.text_main, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, TITLE_OFFSET_Y);

  s_list = lv_list_create(s_screen);
  lv_obj_set_size(s_list, LIST_W, LIST_H);
  lv_obj_align(s_list, LV_ALIGN_CENTER, 0, LIST_OFFSET_Y);
  lv_obj_set_style_bg_color(s_list, current_theme.screen_base, 0);
  lv_obj_set_style_border_color(s_list, ui_theme_get_accent(), 0);

  s_results = ap_scanner_get_results(&s_result_count);

  if (s_results != NULL && s_result_count > 0) {
    for (uint16_t i = 0; i < s_result_count; i++) {
      char buf[AP_LABEL_BUF_SIZE];
      snprintf(buf,
               sizeof(buf),
               "%s (%d) %ddBm",
               s_results[i].ssid,
               s_results[i].primary,
               s_results[i].rssi);

      lv_obj_t *btn = lv_list_add_button(s_list, LV_SYMBOL_WIFI, buf);
      lv_obj_set_style_text_color(btn, current_theme.text_main, 0);
      lv_obj_set_style_bg_color(btn, current_theme.screen_base, 0);
      lv_obj_add_event_cb(btn, on_ap_key_event, LV_EVENT_KEY, &s_results[i]);
    }
  } else {
    lv_list_add_text(s_list, "No networks found.");
  }

  lv_obj_add_event_cb(s_screen, on_screen_key_event, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_list);
    lv_group_focus_obj(s_list);
  }

  lv_screen_load(s_screen);
}