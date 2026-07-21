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

#include "dev_menu_ui.h"

#include "lvgl.h"

#include "buttons_gpio.h"
#include "error_ui.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50

enum { IT_NOTIFY = 0, IT_LORA, IT_WARN, IT_INFO, IT_ERROR, IT_COUNT };

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_timer = NULL;
static bool s_up_last, s_down_last, s_ok_last, s_back_last;

static void fire(int idx) {
  switch (idx) {
    case IT_NOTIFY:
      notify(NOTIFY_UPDATE, "TentacleOS 2.1 ready");
      break;
    case IT_LORA:
      notify(NOTIFY_LORA, "node-2: on my way");
      break;
    case IT_WARN:
      notify(NOTIFY_WARNING, "Battery low - 15%");
      break;
    case IT_INFO:
      notify(NOTIFY_INFO, "Paired with phone");
      break;
    case IT_ERROR:
      error_show("C5 offline", "Co-processor stopped responding");
      break;
    default:
      break;
  }
}

static void tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up(), down = ui_btn_down();
  bool ok = ok_button_is_down(), back = back_button_is_down();

  if (back && !s_back_last) {
    ui_switch_screen(SCREEN_MENU);
    return;
  }
  if (down && !s_down_last) {
    menu_component_next(&s_menu);
    ui_feedback(UI_FB_NAV);
  }
  if (up && !s_up_last) {
    menu_component_prev(&s_menu);
    ui_feedback(UI_FB_NAV);
  }
  if (ok && !s_ok_last)
    fire(menu_component_get_selected(&s_menu));

  s_up_last = up;
  s_down_last = down;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_dev_menu_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_up_last = s_down_last = s_ok_last = s_back_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "DEV", NULL);
  menu_component_add_item(&s_menu, NULL, "Notification");
  menu_component_add_item(&s_menu, NULL, "Notify - LoRa");
  menu_component_add_item(&s_menu, NULL, "Notify - Warning");
  menu_component_add_item(&s_menu, NULL, "Notify - Info");
  menu_component_add_item(&s_menu, NULL, "Error banner");
  menu_component_set_hint(&s_menu, "OK send    BACK exit");

  if (s_timer == NULL)
    s_timer = lv_timer_create(tick_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}
