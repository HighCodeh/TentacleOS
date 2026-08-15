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

#include "sound_settings_ui.h"

#include "esp_log.h"

#include "menu_component_ui.h"
#include "notify_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "SOUND_SETTINGS_UI";

#define ENTRY_FADE_MS 200

#define ROW_VOLUME  0
#define ROW_ALERT   1
#define ROW_KEYBEEP 2
#define ROW_STARTUP 3

static const char *const ALERT_OPTS[] = {"Beep", "Chirp", "Blip", "Off"};
#define ALERT_COUNT ((int)(sizeof(ALERT_OPTS) / sizeof(ALERT_OPTS[0])))

static int s_alert_idx = 0;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static bool s_changed = false;

static void cycle_selector(int sel, int dir) {
  if (sel == ROW_ALERT) {
    s_alert_idx = (s_alert_idx + dir + ALERT_COUNT) % ALERT_COUNT;
    menu_component_set_selector_value(&s_menu, sel, ALERT_OPTS[s_alert_idx]);
    s_changed = true;
  }
}

static void sound_settings_input(const input_event_t *ev, void *ctx) {
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
        if (sel >= 0 && s_menu.has_toggle[sel]) {
          menu_component_toggle_item(&s_menu, sel);
          s_changed = true;
          ESP_LOGI(TAG, "mock toggle row %d -> %d", sel, menu_component_get_toggle(&s_menu, sel));
        }
      }
      break;
    case INPUT_BTN_LEFT:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0) {
          if (s_menu.has_intensity[sel]) {
            menu_component_intensity_dec(&s_menu, sel);
            s_changed = true;
          } else if (s_menu.val_labels[sel] != NULL) {
            cycle_selector(sel, -1);
          }
        }
      }
      break;
    case INPUT_BTN_RIGHT:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0) {
          if (s_menu.has_intensity[sel]) {
            menu_component_intensity_inc(&s_menu, sel);
            s_changed = true;
          } else if (s_menu.val_labels[sel] != NULL) {
            cycle_selector(sel, +1);
          }
        }
      }
      break;
    case INPUT_BTN_BACK:
      if (press) {
        if (s_changed)
          notify(NOTIFY_SAVED, "Sound settings saved");
        ui_switch_screen(SCREEN_SETTINGS);
      }
      break;
    default:
      break;
  }
}

void ui_sound_settings_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_alert_idx = 0;
  s_changed = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "SOUND", "/assets/icons/volume_up.bin");
  menu_component_add_intensity(&s_menu, "/assets/icons/volume_up.bin", "Volume", 4);
  menu_component_add_selector(
      &s_menu, "/assets/icons/notifications_active.bin", "Alert tone", ALERT_OPTS[s_alert_idx]);
  menu_component_add_toggle(&s_menu, "/assets/icons/keyboard.bin", "Key beeps", true);
  menu_component_add_toggle(&s_menu, "/assets/icons/music_note.bin", "Startup sound", false);

  if (s_menu.items_cont != NULL)
    lv_obj_fade_in(s_menu.items_cont, ENTRY_FADE_MS, 0);

  ui_input_set_screen_handler(sound_settings_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
