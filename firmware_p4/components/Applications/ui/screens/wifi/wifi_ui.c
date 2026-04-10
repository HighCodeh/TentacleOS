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

#include "wifi_ui.h"

#include "esp_log.h"

#include "buttons_gpio.h"
#include "lv_port_indev.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_WIFI_MENU";

#define NAV_TIMER_MS 50

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static const struct {
  const char *name;
  const char *icon;
  int target;
} MENU_ITEMS[] = {
    {"SCAN NETWORKS", NULL, SCREEN_WIFI_SCAN_MENU},
    {"ATTACKS", NULL, SCREEN_WIFI_ATTACK_MENU},
    {"PACKETS CAPTURE", NULL, SCREEN_WIFI_PACKETS_MENU},
    {"EVIL-TWIN", NULL, SCREEN_WIFI_EVIL_TWIN},
};

#define MENU_ITEMS_COUNT (sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]))

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool is_up = up_button_is_down();
  bool is_down = down_button_is_down();
  bool is_left = left_button_is_down();
  bool is_right = right_button_is_down();
  bool is_ok = ok_button_is_down();
  bool is_back = back_button_is_down();

  if (is_down && !s_btn_down_last) {
    menu_component_next(&s_menu);
  }
  if (is_up && !s_btn_up_last) {
    menu_component_prev(&s_menu);
  }
  if ((is_back && !s_btn_back_last) || (is_left && !s_btn_left_last)) {
    ui_switch_screen(SCREEN_MENU);
  }
  if ((is_ok && !s_btn_ok_last) || (is_right && !s_btn_right_last)) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0 && sel < (int)MENU_ITEMS_COUNT) {
      ui_switch_screen(MENU_ITEMS[sel].target);
    }
  }

  s_btn_up_last = is_up;
  s_btn_down_last = is_down;
  s_btn_left_last = is_left;
  s_btn_right_last = is_right;
  s_btn_ok_last = is_ok;
  s_btn_back_last = is_back;
}

void ui_wifi_menu_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "WIFI", "/assets/icons/wifi_menu_icon.bin");

  for (int i = 0; i < (int)MENU_ITEMS_COUNT; i++) {
    menu_component_add_item(&s_menu, "/assets/icons/wifi_menu_icon.bin", MENU_ITEMS[i].name);
  }

  if (s_nav_timer == NULL) {
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
  }

  lv_screen_load(s_screen);
}
