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

#include "menu_component_ui.h"
#include "notify_ui.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "INTERFACE_SETTINGS_UI";

#define ENTRY_FADE_MS 200

#define ROW_HAPTICS 0
#define ROW_THEME   1
#define ROW_LED     2

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static bool s_changed = false;

static void interface_settings_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_OK:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel == ROW_THEME) {
          ui_switch_screen(SCREEN_THEME_SELECTOR);
        } else if (sel == ROW_LED) {
          ui_switch_screen(SCREEN_LED_CTRL);
        } else if (sel == ROW_HAPTICS) {
          menu_component_toggle_item(&s_menu, sel);
          g_config_system.vibration = menu_component_get_toggle(&s_menu, sel);
          s_changed = true;
          ESP_LOGI(TAG, "haptics -> %d", g_config_system.vibration);
        }
      }
      break;
    case INPUT_BTN_LEFT:
      if (press) {
        /* No value selectors on this screen. */
      }
      break;
    case INPUT_BTN_RIGHT:
      if (press) {
        /* No value selectors on this screen. */
      }
      break;
    case INPUT_BTN_BACK:
      if (press) {
        if (s_changed) {
          if (tos_config_save(TOS_PATH_CONFIG_SYSTEM, "system") == ESP_OK)
            notify(NOTIFY_SAVED, "Interface settings saved");
          else
            notify(NOTIFY_WARNING, "Save failed (no SD?)");
        }
        ui_switch_screen(SCREEN_SETTINGS);
      }
      break;
    default:
      break;
  }
}

void ui_interface_settings_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_changed = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "INTERFACE", "/assets/icons/tune.bin");
  menu_component_add_toggle(
      &s_menu, "/assets/icons/vibration.bin", "Haptics", g_config_system.vibration);
  menu_component_add_item(&s_menu, "/assets/icons/palette.bin", "Theme");
  menu_component_add_item(&s_menu, "/assets/icons/sensors.bin", "LED / Notify");

  if (s_menu.items_cont != NULL)
    lv_obj_fade_in(s_menu.items_cont, ENTRY_FADE_MS, 0);

  ui_input_set_screen_handler(interface_settings_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
