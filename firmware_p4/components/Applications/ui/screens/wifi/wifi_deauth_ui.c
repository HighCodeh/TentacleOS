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

#include "wifi_deauth_ui.h"

#include <string.h>

#include "esp_log.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_deauther.h"

static const char *TAG = "UI_DEAUTH";

#define TITLE_OFFSET_Y  30
#define TARGET_OFFSET_Y (-20)
#define BTN_WIDTH       140
#define BTN_HEIGHT      40
#define BTN_OFFSET_Y    30
#define STATUS_OFFSET_Y (-30)
#define BTN_BORDER_W    2

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_btn_start = NULL;
static lv_obj_t *s_lbl_status = NULL;
static wifi_ap_record_t s_target_ap;
static bool s_is_target_set = false;

extern lv_group_t *main_group;

void ui_wifi_deauth_set_target(wifi_ap_record_t *ap) {
  if (ap != NULL) {
    memcpy(&s_target_ap, ap, sizeof(wifi_ap_record_t));
    s_is_target_set = true;
  }
}

static void toggle_attack_handler(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER) {
    if (wifi_deauther_is_running()) {
      wifi_deauther_stop();
      lv_label_set_text(lv_obj_get_child(s_btn_start, 0), "START ATTACK");
      lv_obj_set_style_bg_color(s_btn_start, current_theme.bg_item_top, 0);
      lv_label_set_text(s_lbl_status, "Status: IDLE");
    } else {
      if (!s_is_target_set) {
        lv_label_set_text(s_lbl_status, "No Target Selected!");
        return;
      }
      wifi_deauther_start(&s_target_ap, WIFI_DEAUTHER_TYPE_INVALID_AUTH, true);
      lv_label_set_text(lv_obj_get_child(s_btn_start, 0), "STOP ATTACK");
      lv_obj_set_style_bg_color(s_btn_start, current_theme.bg_item_bot, 0);
      lv_label_set_text(s_lbl_status, "Status: ATTACKING...");
    }
  }
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_KEY) {
    if (lv_event_get_key(e) == LV_KEY_ESC) {
      if (wifi_deauther_is_running()) {
        wifi_deauther_stop();
      }
      ui_switch_screen(SCREEN_WIFI_AP_LIST);
    }
  }
}

void ui_wifi_deauth_open(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  lv_obj_t *title = lv_label_create(s_screen);
  lv_label_set_text(title, "DEAUTH ATTACK");
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, TITLE_OFFSET_Y);

  lv_obj_t *lbl_target = lv_label_create(s_screen);
  if (s_is_target_set) {
    lv_label_set_text_fmt(lbl_target,
                          "Target: %s\nCh: %d  RSSI: %d",
                          s_target_ap.ssid,
                          s_target_ap.primary,
                          s_target_ap.rssi);
  } else {
    lv_label_set_text(lbl_target, "No Target Selected");
  }
  lv_obj_set_style_text_align(lbl_target, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl_target, current_theme.text_main, 0);
  lv_obj_align(lbl_target, LV_ALIGN_CENTER, 0, TARGET_OFFSET_Y);

  s_btn_start = lv_btn_create(s_screen);
  lv_obj_set_size(s_btn_start, BTN_WIDTH, BTN_HEIGHT);
  lv_obj_align(s_btn_start, LV_ALIGN_CENTER, 0, BTN_OFFSET_Y);
  lv_obj_set_style_bg_color(s_btn_start, current_theme.bg_item_top, 0);
  lv_obj_set_style_border_color(s_btn_start, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(s_btn_start, BTN_BORDER_W, 0);

  lv_obj_t *lbl_btn = lv_label_create(s_btn_start);
  lv_label_set_text(lbl_btn, "START ATTACK");
  lv_obj_center(lbl_btn);

  s_lbl_status = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_status, "Status: READY");
  lv_obj_set_style_text_color(s_lbl_status, current_theme.text_main, 0);
  lv_obj_align(s_lbl_status, LV_ALIGN_BOTTOM_MID, 0, STATUS_OFFSET_Y);

  lv_obj_add_event_cb(s_btn_start, toggle_attack_handler, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_btn_start);
    lv_group_focus_obj(s_btn_start);
  }

  lv_screen_load(s_screen);
}
