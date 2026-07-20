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

#include "games_menu_ui.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50
#define GAME_ICON    "/assets/icons/game_icon.bin"

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static bool s_up_last, s_down_last, s_left_last, s_right_last, s_ok_last, s_back_last;

static const struct {
  const char *name;
  int target;
} ITEMS[] = {
    {"OCTO PET", SCREEN_GAME_OCTOPET},
    {"OCTO FLAP", SCREEN_GAME_FLAPPY},
    {"SNAKE", SCREEN_GAME_SNAKE},
    {"BREAKOUT", SCREEN_GAME_BREAKOUT},
};
#define ITEM_COUNT ((int)(sizeof(ITEMS) / sizeof(ITEMS[0])))

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up(), down = ui_btn_down(), left = ui_btn_left();
  bool right = ui_btn_right(), ok = ok_button_is_down(), back = back_button_is_down();

  if (down && !s_down_last)
    menu_component_next(&s_menu);
  if (up && !s_up_last)
    menu_component_prev(&s_menu);
  if ((back && !s_back_last) || (left && !s_left_last))
    ui_switch_screen(SCREEN_MENU);
  if ((ok && !s_ok_last) || (right && !s_right_last)) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0 && sel < ITEM_COUNT)
      ui_switch_screen(ITEMS[sel].target);
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_games_menu_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "GAMES", GAME_ICON);
  for (int i = 0; i < ITEM_COUNT; i++)
    menu_component_add_item(&s_menu, GAME_ICON, ITEMS[i].name);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}
