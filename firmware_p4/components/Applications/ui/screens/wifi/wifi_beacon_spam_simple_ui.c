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

#include "wifi_beacon_spam_simple_ui.h"

#include "esp_log.h"
#include "lvgl.h"

#include "beacon_spam.h"
#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_BEACON_SPAM_SIMPLE";

#define STATUS_OFFSET_Y (-10)
#define COUNT_OFFSET_Y  20
#define COUNT_INCREMENT 10
#define TIMER_PERIOD_MS 1000

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_lbl_count = NULL;
static lv_timer_t *s_update_timer = NULL;
static uint32_t s_spam_count = 0;

extern lv_group_t *main_group;

static void update_count_cb(lv_timer_t *t) {
  (void)t;
  if (!beacon_spam_is_running())
    return;
  s_spam_count += COUNT_INCREMENT;
  if (s_lbl_count != NULL) {
    lv_label_set_text_fmt(s_lbl_count, "Created: %lu", (unsigned long)s_spam_count);
  }
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    if (s_update_timer != NULL) {
      lv_timer_del(s_update_timer);
      s_update_timer = NULL;
    }
    beacon_spam_stop();
    ui_switch_screen(SCREEN_WIFI_ATTACK_MENU);
  }
}

void ui_wifi_beacon_spam_simple_open(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  s_lbl_status = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_status, "Spamming Random SSIDs...");
  lv_obj_set_style_text_color(s_lbl_status, current_theme.text_main, 0);
  lv_obj_align(s_lbl_status, LV_ALIGN_CENTER, 0, STATUS_OFFSET_Y);

  s_lbl_count = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_count, "Created: 0");
  lv_obj_set_style_text_color(s_lbl_count, current_theme.text_main, 0);
  lv_obj_align(s_lbl_count, LV_ALIGN_CENTER, 0, COUNT_OFFSET_Y);

  s_spam_count = 0;
  beacon_spam_start_random();

  if (s_update_timer != NULL)
    lv_timer_del(s_update_timer);
  s_update_timer = lv_timer_create(update_count_cb, TIMER_PERIOD_MS, NULL);

  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_screen);
    lv_group_focus_obj(s_screen);
  }

  lv_screen_load(s_screen);
}
