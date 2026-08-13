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

#include "nfc_menu_ui.h"

#include "esp_log.h"

#include "ui_theme.h"
#include "menu_component_ui.h"
#include "ui_manager.h"

static const char *TAG = "NFC_MENU_UI";

typedef struct {
  const char *name;
  const char *icon;
  int target;
} nfc_menu_item_t;

static const nfc_menu_item_t ITEMS[] = {
    {"READ TAGS", "/assets/icons/contactless.bin", SCREEN_NFC_READ},
    {"SCAN / IDENTIFY", "/assets/icons/sensors.bin", SCREEN_NFC_SCAN},
    {"EMULATE", "/assets/icons/contactless.bin", SCREEN_CARD_EMU},
    {"WRITE", "/assets/icons/edit.bin", SCREEN_NFC_WRITE},
    {"CONFIGURATIONS", "/assets/icons/settings.bin", SCREEN_NFC_CONFIG},
    {"SAVED", "/assets/icons/bookmarks.bin", SCREEN_NFC_SAVED},
    {"BANK CARD", "/assets/icons/contactless.bin", SCREEN_NFC_BANKCARD},
    {"DESFIRE", "/assets/icons/sensors.bin", SCREEN_NFC_DESFIRE},
    {"NFC-V / 15693", "/assets/icons/contactless.bin", SCREEN_NFC_ISO15693},
    {"ULTRALIGHT/NTAG", "/assets/icons/contactless.bin", SCREEN_NFC_ULTRALIGHT},
    {"NDEF", "/assets/icons/description.bin", SCREEN_NFC_NDEF},
    {"FELICA", "/assets/icons/contactless.bin", SCREEN_NFC_FELICA},
    {"SHARE (P2P)", "/assets/icons/podcasts.bin", SCREEN_NFC_P2P},
    {"KEY DICTIONARY", "/assets/icons/settings.bin", SCREEN_NFC_KEYDICT},
};
#define ITEM_COUNT (sizeof(ITEMS) / sizeof(ITEMS[0]))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

// Event-driven input (see ui_input_set_screen_handler). The central pump only
// calls this while input is unlocked and no modal overlay is up, so the old
// per-frame guards are gone. UP/DOWN also act on REPEAT for held auto-scroll.
static void nfc_menu_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0 && (size_t)sel < ITEM_COUNT && ITEMS[sel].target >= 0) {
          ui_switch_screen(ITEMS[sel].target);
        }
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
    default:
      break;
  }
}

void ui_nfc_menu_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "NFC", "/assets/icons/nfc.bin");

  for (size_t i = 0; i < ITEM_COUNT; i++) {
    menu_component_add_item(&s_menu, ITEMS[i].icon, ITEMS[i].name);
  }

  ui_input_set_screen_handler(nfc_menu_input, NULL);

  ui_screen_load(s_screen);
}
