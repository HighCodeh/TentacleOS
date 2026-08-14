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

#include "display_settings_ui.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "st7789.h"
#include "tos_config.h"
#include "tos_flash_paths.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS  50
#define ENTRY_FADE_MS 200

#define ROW_BRIGHTNESS 0
#define ROW_ROTATION   1
#define ROW_TIMEOUT    2
#define ROW_AUTODIM    3
#define ROW_INVERT     4

// Brightness maps the 5-step intensity bar to a percentage (level * 20).
#define BRIGHTNESS_STEP_PCT 20

static const char *const ROTATION_OPTS[] = {"Portrait", "Landscape"};
#define ROTATION_COUNT ((int)(sizeof(ROTATION_OPTS) / sizeof(ROTATION_OPTS[0])))

// Labels and their auto_lock_seconds values (0 = never sleep).
static const char *const TIMEOUT_OPTS[] = {"15s", "30s", "1m", "5m", "Off"};
static const int TIMEOUT_SECS[] = {15, 30, 60, 300, 0};
#define TIMEOUT_COUNT ((int)(sizeof(TIMEOUT_OPTS) / sizeof(TIMEOUT_OPTS[0])))

static int s_rotation_idx = 0;
static int s_timeout_idx = 1;

// Pick the timeout option index matching a saved auto_lock_seconds value.
static int timeout_idx_for_seconds(int seconds) {
  for (int i = 0; i < TIMEOUT_COUNT; i++) {
    if (TIMEOUT_SECS[i] == seconds) {
      return i;
    }
  }
  return TIMEOUT_COUNT - 1;  // fall back to "Off"
}

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
  if (sel == ROW_ROTATION) {
    s_rotation_idx = (s_rotation_idx + dir + ROTATION_COUNT) % ROTATION_COUNT;
    menu_component_set_selector_value(&s_menu, sel, ROTATION_OPTS[s_rotation_idx]);
    s_changed = true;
  } else if (sel == ROW_TIMEOUT) {
    s_timeout_idx = (s_timeout_idx + dir + TIMEOUT_COUNT) % TIMEOUT_COUNT;
    menu_component_set_selector_value(&s_menu, sel, TIMEOUT_OPTS[s_timeout_idx]);
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
    if (sel >= 0 && s_menu.has_toggle[sel]) {
      menu_component_toggle_item(&s_menu, sel);
      s_changed = true;
    }
  }

  if (left && !s_left_last) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0) {
      if (s_menu.has_intensity[sel]) {
        menu_component_intensity_dec(&s_menu, sel);
        if (sel == ROW_BRIGHTNESS)
          lcd_apply_brightness(menu_component_get_intensity(&s_menu, sel) * BRIGHTNESS_STEP_PCT);
        s_changed = true;
      } else if (s_menu.val_labels[sel] != NULL) {
        cycle_selector(sel, -1);
      }
    }
  }
  if (right && !s_right_last) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0) {
      if (s_menu.has_intensity[sel]) {
        menu_component_intensity_inc(&s_menu, sel);
        if (sel == ROW_BRIGHTNESS)
          lcd_apply_brightness(menu_component_get_intensity(&s_menu, sel) * BRIGHTNESS_STEP_PCT);
        s_changed = true;
      } else if (s_menu.val_labels[sel] != NULL) {
        cycle_selector(sel, +1);
      }
    }
  }

  if (back && !s_back_last) {
    if (s_changed) {
      // Commit to the in-memory config and persist the full "screen" schema.
      // tos_config is the sole writer of this file (st7789 only reads it), so
      // brightness, auto-lock and auto-dim stay in one consistent file.
      g_config_screen.brightness =
          menu_component_get_intensity(&s_menu, ROW_BRIGHTNESS) * BRIGHTNESS_STEP_PCT;
      g_config_screen.auto_lock_seconds = TIMEOUT_SECS[s_timeout_idx];
      g_config_screen.auto_dim = menu_component_get_toggle(&s_menu, ROW_AUTODIM);
      tos_config_save(FLASH_CONFIG_SCREEN, "screen");
      notify(NOTIFY_SAVED, "Display settings saved");
    }
    ui_switch_screen(SCREEN_SETTINGS);
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_display_settings_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_rotation_idx = 0;
  s_timeout_idx = timeout_idx_for_seconds(g_config_screen.auto_lock_seconds);
  s_changed = false;

  int bright_level = lcd_get_brightness() / BRIGHTNESS_STEP_PCT;
  if (bright_level < 0)
    bright_level = 0;
  if (bright_level > 5)
    bright_level = 5;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "DISPLAY", "/assets/icons/display_settings.bin");
  menu_component_add_intensity(&s_menu, "/assets/icons/brightness_6.bin", "Brightness", bright_level);
  menu_component_add_selector(
      &s_menu, "/assets/icons/screen_rotation.bin", "Rotation", ROTATION_OPTS[s_rotation_idx]);
  menu_component_add_selector(
      &s_menu, "/assets/icons/timer.bin", "Timeout", TIMEOUT_OPTS[s_timeout_idx]);
  menu_component_add_toggle(
      &s_menu, "/assets/icons/brightness_auto.bin", "Auto-dim", g_config_screen.auto_dim);
  menu_component_add_toggle(&s_menu, "/assets/icons/invert_colors.bin", "Invert", false);

  if (s_menu.items_cont != NULL)
    lv_obj_fade_in(s_menu.items_cont, ENTRY_FADE_MS, 0);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}
