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

#include "wifi_probe_flood_ui.h"

#include "esp_log.h"
#include "lvgl.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_flood.h"

static const char *TAG = "UI_PROBE_FLOOD";

#define STATUS_OFFSET_Y  (-30)
#define CHANNEL_OFFSET_Y (-10)
#define BTN_WIDTH        140
#define BTN_HEIGHT       40
#define BTN_OFFSET_Y     45
#define BTN_BORDER_W     2
#define CHANNEL_MIN      1
#define CHANNEL_MAX      13

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_btn_toggle = NULL;
static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_lbl_channel = NULL;
static bool s_is_running = false;
static uint8_t s_current_channel = CHANNEL_MIN;

extern lv_group_t *main_group;

static void update_ui(void) {
  if (s_lbl_status != NULL) {
    lv_label_set_text(s_lbl_status, s_is_running ? "Status: FLOODING..." : "Status: IDLE");
    lv_obj_set_style_text_color(s_lbl_status, current_theme.text_main, 0);
  }
  if (s_lbl_channel != NULL) {
    lv_label_set_text_fmt(s_lbl_channel, "Channel: %d", s_current_channel);
  }
  if (s_btn_toggle != NULL) {
    lv_label_set_text(lv_obj_get_child(s_btn_toggle, 0), s_is_running ? "STOP" : "START");
    lv_obj_set_style_bg_color(s_btn_toggle, current_theme.border_accent, 0);
  }
}

static void toggle_handler(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  if (lv_event_get_key(e) != LV_KEY_ENTER)
    return;

  if (s_is_running) {
    wifi_flood_stop();
    s_is_running = false;
  } else {
    wifi_flood_probe_start(NULL, s_current_channel);
    s_is_running = true;
  }
  update_ui();
}

static void screen_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    if (s_is_running) {
      wifi_flood_stop();
      s_is_running = false;
    }
    ui_switch_screen(SCREEN_WIFI_ATTACK_MENU);
  } else if (!s_is_running && (key == LV_KEY_RIGHT || key == LV_KEY_UP || key == LV_KEY_DOWN)) {
    if (key == LV_KEY_RIGHT || key == LV_KEY_UP)
      s_current_channel = (s_current_channel % CHANNEL_MAX) + 1;
    else
      s_current_channel =
          (s_current_channel == CHANNEL_MIN) ? CHANNEL_MAX : (s_current_channel - 1);
    update_ui();
  }
}

void ui_wifi_probe_flood_open(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  s_lbl_status = lv_label_create(s_screen);
  lv_label_set_text(s_lbl_status, "Status: IDLE");
  lv_obj_set_style_text_color(s_lbl_status, current_theme.text_main, 0);
  lv_obj_align(s_lbl_status, LV_ALIGN_CENTER, 0, STATUS_OFFSET_Y);

  s_lbl_channel = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_lbl_channel, "Channel: %d", s_current_channel);
  lv_obj_set_style_text_color(s_lbl_channel, current_theme.text_main, 0);
  lv_obj_align(s_lbl_channel, LV_ALIGN_CENTER, 0, CHANNEL_OFFSET_Y);

  s_btn_toggle = lv_btn_create(s_screen);
  lv_obj_set_size(s_btn_toggle, BTN_WIDTH, BTN_HEIGHT);
  lv_obj_align(s_btn_toggle, LV_ALIGN_CENTER, 0, BTN_OFFSET_Y);
  lv_obj_set_style_bg_color(s_btn_toggle, current_theme.border_accent, 0);
  lv_obj_set_style_border_color(s_btn_toggle, ui_theme_get_accent(), 0);
  lv_obj_set_style_border_width(s_btn_toggle, BTN_BORDER_W, 0);

  lv_obj_t *lbl_btn = lv_label_create(s_btn_toggle);
  lv_label_set_text(lbl_btn, "START");
  lv_obj_center(lbl_btn);

  lv_obj_add_event_cb(s_btn_toggle, toggle_handler, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_btn_toggle, screen_event_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_btn_toggle);
    lv_group_focus_obj(s_btn_toggle);
  }

  update_ui();
  lv_screen_load(s_screen);
}
