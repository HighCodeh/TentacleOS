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

#include "nfc_config_ui.h"

#include "lvgl.h"

#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

enum { CFG_FIELD, CFG_POLL, CFG_AAT, CFG_DIAG, CFG_COUNT };
static const char *const POLL_NAMES[] = {"Slow", "Normal", "Fast"};
#define POLL_N 3

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static int s_poll = 1;

static void nfc_config_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  int sel = menu_component_get_selected(&s_menu);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_NFC_MENU);
      break;
    case INPUT_BTN_LEFT:
      if (press) {
        if (sel == CFG_POLL) {
          s_poll = (s_poll - 1 + POLL_N) % POLL_N;
          menu_component_set_selector_value(&s_menu, CFG_POLL, POLL_NAMES[s_poll]);
        }
      }
      break;
    case INPUT_BTN_RIGHT:
      if (press && sel == CFG_POLL) {
        s_poll = (s_poll + 1) % POLL_N;
        menu_component_set_selector_value(&s_menu, CFG_POLL, POLL_NAMES[s_poll]);
      }
      break;
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
        if (sel == CFG_FIELD) {
          menu_component_toggle_item(&s_menu, CFG_FIELD);
        } else if (sel == CFG_AAT) {
          notify(NOTIFY_SAVED, "Antenna tuned");
        } else if (sel == CFG_DIAG) {
          msgbox_open(
              LV_SYMBOL_WARNING,
              "ST25R3916: no reply\nSPI3 MISO blocked\n(GPIO36 jumper) -\nrunning simulated",
              NULL,
              NULL,
              NULL);
        }
      }
      break;
    default:
      break;
  }
}

void ui_nfc_config_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "NFC Config", "/assets/icons/settings.bin");
  menu_component_add_toggle(
      &s_menu, "/assets/icons/power_settings_new.bin", "Field on boot", false);
  menu_component_add_selector(
      &s_menu, "/assets/icons/autorenew.bin", "Poll rate", POLL_NAMES[s_poll]);
  menu_component_add_item(&s_menu, "/assets/icons/settings_input_antenna.bin", "Antenna Tune");
  menu_component_add_item(&s_menu, "/assets/icons/troubleshoot.bin", "Bus Diagnostic");

  ui_input_set_screen_handler(nfc_config_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
