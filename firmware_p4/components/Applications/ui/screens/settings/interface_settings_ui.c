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

#include "interface_settings_ui.h"

#include "esp_log.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "INTERFACE_SETTINGS_UI";

#define NAV_TIMER_MS  50
#define ENTRY_FADE_MS 200

#define ROW_ANIMATIONS 0
#define ROW_HAPTICS    1
#define ROW_SOUNDFX    2
#define ROW_THEME      3
#define ROW_LANGUAGE   4

static const char *const LANGUAGE_OPTS[] = {"EN", "PT", "ES"};
#define LANGUAGE_COUNT ((int)(sizeof(LANGUAGE_OPTS) / sizeof(LANGUAGE_OPTS[0])))

static int s_language_idx = 0;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_left_last = false;
static bool s_right_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;
static bool s_changed = false;

static void cycle_selector(int sel, int dir) {
  if (sel == ROW_LANGUAGE) {
    s_language_idx = (s_language_idx + dir + LANGUAGE_COUNT) % LANGUAGE_COUNT;
    menu_component_set_selector_value(&s_menu, sel, LANGUAGE_OPTS[s_language_idx]);
    s_changed = true;
  }
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (down && !s_down_last)
    menu_component_next(&s_menu);
  if (up && !s_up_last)
    menu_component_prev(&s_menu);

  if (ok && !s_ok_last) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel == ROW_THEME) {
      ui_switch_screen(SCREEN_THEME_SELECTOR);
      return;
    }
    if (sel >= 0 && s_menu.has_toggle[sel]) {
      menu_component_toggle_item(&s_menu, sel);
      s_changed = true;
      ESP_LOGI(TAG, "mock toggle row %d -> %d", sel, menu_component_get_toggle(&s_menu, sel));
    }
  }

  if (left && !s_left_last) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0) {
      if (s_menu.has_intensity[sel])
        menu_component_intensity_dec(&s_menu, sel);
      else if (s_menu.val_labels[sel] != NULL)
        cycle_selector(sel, -1);
    }
  }
  if (right && !s_right_last) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0) {
      if (s_menu.has_intensity[sel])
        menu_component_intensity_inc(&s_menu, sel);
      else if (s_menu.val_labels[sel] != NULL)
        cycle_selector(sel, +1);
    }
  }

  if (back && !s_back_last) {
    if (s_changed)
      notify(NOTIFY_SAVED, "Interface settings saved");
    ui_switch_screen(SCREEN_SETTINGS);
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_interface_settings_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_language_idx = 0;
  s_changed = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "INTERFACE", "/assets/icons/interface_menu_icon.bin");
  menu_component_add_toggle(&s_menu, NULL, "Animations", true);
  menu_component_add_toggle(&s_menu, NULL, "Haptics", true);
  menu_component_add_toggle(&s_menu, NULL, "Sound FX", true);
  menu_component_add_item(&s_menu, "/assets/icons/theme_menu_icon.bin", "Theme");
  menu_component_add_selector(&s_menu, NULL, "Language", LANGUAGE_OPTS[s_language_idx]);

  if (s_menu.items_cont != NULL)
    lv_obj_fade_in(s_menu.items_cont, ENTRY_FADE_MS, 0);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}
