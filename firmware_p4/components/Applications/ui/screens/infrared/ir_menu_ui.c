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

#include "ir_menu_ui.h"

#include "esp_log.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "IR_MENU_UI";

#define NAV_TIMER_INTERVAL_MS 50

typedef struct {
  const char *name;
  const char *icon;
  int target;
} ir_menu_item_t;

static const ir_menu_item_t MENU_ITEMS[] = {
    {"Learn", "/assets/icons/learn_icon.bin", SCREEN_IR_RECEIVE},
    {"Send", "/assets/icons/ir_send_menu_icon.bin", SCREEN_IR_SEND},
    {"Remote Control", "/assets/icons/remote_menu_icon.bin", SCREEN_IR_REMOTE_TYPE},
    {"Browse Signals", "/assets/icons/search_menu_icon.bin", SCREEN_IR_SAVED},
    {"Burst", "/assets/icons/burst_menu_icon.bin", SCREEN_IR_BURST},
};
#define MENU_ITEMS_COUNT (sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *timer);

void ui_ir_menu_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "INFRARED", "/assets/icons/ir_icon.bin");
  for (size_t i = 0; i < MENU_ITEMS_COUNT; i++)
    menu_component_add_item(&s_menu, MENU_ITEMS[i].icon, MENU_ITEMS[i].name);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);

  ui_screen_load(s_screen);
}

static void nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    s_nav_timer = NULL;
    return;
  }

  if (ui_input_is_locked())
    return;

  bool is_up = ui_btn_up();
  bool is_down = ui_btn_down();
  bool is_ok = ok_button_is_down();
  bool is_back = back_button_is_down();

  if (is_down && !s_btn_down_last)
    menu_component_next(&s_menu);

  if (is_up && !s_btn_up_last)
    menu_component_prev(&s_menu);

  if (is_back && !s_btn_back_last)
    ui_switch_screen(SCREEN_MENU);

  if (is_ok && !s_btn_ok_last) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0 && sel < (int)MENU_ITEMS_COUNT && MENU_ITEMS[sel].target >= 0)
      ui_switch_screen(MENU_ITEMS[sel].target);
  }

  s_btn_up_last = is_up;
  s_btn_down_last = is_down;
  s_btn_ok_last = is_ok;
  s_btn_back_last = is_back;
}
