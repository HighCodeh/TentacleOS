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

#include "wifi_names_ui.h"

#include "lvgl.h"

#include "buttons_gpio.h"
#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_names.h"

#define NAV_TIMER_MS 50
#define NAME_ICON    "/assets/icons/wifi_menu_icon.bin"
#define COLOR_NAME   0x00E676
#define COLOR_ADD    0xCC00FF

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static int s_edit_index = -1;

static bool s_up_last, s_down_last, s_left_last, s_right_last, s_ok_last, s_back_last;

static void nav_timer_cb(lv_timer_t *t);

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "Networks", NAME_ICON);

  int count = wifi_names_count();
  for (int i = 0; i < count; i++) {
    menu_component_add_item(&s_menu, NAME_ICON, wifi_names_get(i));
    menu_component_set_item_label_color(&s_menu, i, lv_color_hex(COLOR_NAME));
  }

  if (count < WIFI_NAMES_MAX) {
    menu_component_add_item(&s_menu, NAME_ICON, "+ Add network");
    menu_component_set_item_label_color(&s_menu, count, lv_color_hex(COLOR_ADD));
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void rebuild_async(void *unused) {
  (void)unused;
  if (ui_current_screen() == SCREEN_WIFI_NAMES)
    build_screen();
}

static void on_kb_submit(const char *text, void *ud) {
  (void)ud;
  int count = wifi_names_count();
  if (s_edit_index == count) {
    wifi_names_add(text);
  } else if (s_edit_index >= 0 && s_edit_index < count) {
    if (text && text[0])
      wifi_names_set(s_edit_index, text);
    else
      wifi_names_remove(s_edit_index);
  }
  s_edit_index = -1;
  lv_async_call(rebuild_async, NULL);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (keyboard_is_open() || ui_input_is_locked()) {
    s_up_last = up;
    s_down_last = down;
    s_left_last = left;
    s_right_last = right;
    s_ok_last = ok;
    s_back_last = back;
    return;
  }

  if (down && !s_down_last)
    menu_component_next(&s_menu);
  if (up && !s_up_last)
    menu_component_prev(&s_menu);

  if ((back && !s_back_last) || (left && !s_left_last))
    ui_switch_screen(SCREEN_WIFI_MENU);

  if ((ok && !s_ok_last) || (right && !s_right_last)) {
    int sel = menu_component_get_selected(&s_menu);
    int count = wifi_names_count();
    if (sel >= 0 && sel <= count && !(sel == count && count >= WIFI_NAMES_MAX)) {
      s_edit_index = sel;
      keyboard_open(NULL, on_kb_submit, NULL);
    }
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_wifi_names_open(void) {
  wifi_names_init();
  s_edit_index = -1;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;
  build_screen();
}
