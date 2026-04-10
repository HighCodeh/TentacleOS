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
#include "wifi_beacon_spam_ui.h"

#include "esp_log.h"

#include "beacon_spam.h"
#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "tos_flash_paths.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_BEACON_SPAM";

#define DEFAULT_LIST_PATH  FLASH_STORAGE_WIFI_BEACONS
#define BTN_WIDTH          160
#define BTN_HEIGHT         40
#define TITLE_OFFSET_Y     30
#define BTN_MODE_OFFSET_Y  (-20)
#define BTN_START_OFFSET_Y 30
#define STATUS_OFFSET_Y    (-30)

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_btn_mode = NULL;
static lv_obj_t *s_btn_start = NULL;
static lv_obj_t *s_lbl_status = NULL;
static bool s_is_random_mode = true;

extern lv_group_t *main_group;

static void update_mode_label(void) {
  if (s_btn_mode != NULL) {
    lv_label_set_text_fmt(
        lv_obj_get_child(s_btn_mode, 0), "Mode: %s", s_is_random_mode ? "RANDOM" : "LIST");
  }
}

static void toggle_mode_handler(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_KEY &&
      (lv_event_get_key(e) == LV_KEY_ENTER || lv_event_get_key(e) == LV_KEY_RIGHT ||
       lv_event_get_key(e) == LV_KEY_LEFT)) {
    if (!beacon_spam_is_running()) {
      s_is_random_mode = !s_is_random_mode;
      update_mode_label();
    }
  }
}

static void toggle_start_handler(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER) {
    if (beacon_spam_is_running()) {
      beacon_spam_stop();
      lv_label_set_text(lv_obj_get_child(s_btn_start, 0), "START SPAM");
      lv_obj_set_style_bg_color(s_btn_start, current_theme.bg_item_top, 0);
      lv_label_set_text(s_lbl_status, "Status: STOPPED");
      if (s_btn_mode != NULL)
        lv_obj_clear_state(s_btn_mode, LV_STATE_DISABLED);
    } else {
      bool is_success = false;
      if (s_is_random_mode) {
        is_success = beacon_spam_start_random();
      } else {
        is_success = beacon_spam_start_custom(DEFAULT_LIST_PATH);
      }

      if (is_success) {
        lv_label_set_text(lv_obj_get_child(s_btn_start, 0), "STOP SPAM");
        lv_obj_set_style_bg_color(s_btn_start, current_theme.bg_item_bot, 0);
        lv_label_set_text(s_lbl_status, "Status: SPAMMING...");
        if (s_btn_mode != NULL)
          lv_obj_add_state(s_btn_mode, LV_STATE_DISABLED);
      } else {
        lv_label_set_text(s_lbl_status, "Failed to start!");
      }
    }
  }
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_KEY) {
    if (lv_event_get_key(e) == LV_KEY_ESC) {
      if (beacon_spam_is_running()) {
        beacon_spam_stop();
      }
      ui_switch_screen(SCREEN_WIFI_MENU);
    }
  }
}

void ui_wifi_beacon_spam_open(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  lv_obj_t *title = lv_label_create(s_screen);
  lv_label_set_text(title, "BEACON SPAM");
  lv_obj_set_style_text_color(title, current_theme.text_main, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, TITLE_OFFSET_Y);

  s_btn_mode = lv_btn_create(s_screen);
  lv_obj_set_size(s_btn_mode, BTN_WIDTH, BTN_HEIGHT);
  lv_obj_align(s_btn_mode, LV_ALIGN_CENTER, 0, BTN_MODE_OFFSET_Y);

  lv_obj_t *lbl_mode = lv_label_create(s_btn_mode);
  lv_obj_center(lbl_mode);
  update_mode_label();

  s_btn_start = lv_btn_create(s_screen);
  lv_obj_set_size(s_btn_start, BTN_WIDTH, BTN_HEIGHT);
  lv_obj_align(s_btn_start, LV_ALIGN_CENTER, 0, BTN_START_OFFSET_Y);
  lv_obj_set_style_bg_color(s_btn_start, current_theme.bg_item_top, 0);

  lv_obj_t *lbl_btn = lv_label_create(s_btn_start);
  lv_label_set_text(lbl_btn, "START SPAM");
  lv_obj_center(lbl_btn);

  s_lbl_status = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_status, "Status: READY");
  lv_obj_set_style_text_color(s_lbl_status, current_theme.text_main, 0);
  lv_obj_align(s_lbl_status, LV_ALIGN_BOTTOM_MID, 0, STATUS_OFFSET_Y);

  lv_obj_add_event_cb(s_btn_mode, toggle_mode_handler, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_btn_start, toggle_start_handler, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_btn_mode);
    lv_group_add_obj(main_group, s_btn_start);
    lv_group_focus_obj(s_btn_mode);
  }

  lv_screen_load(s_screen);
}
