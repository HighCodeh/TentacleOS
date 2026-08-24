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

#include "ble_detect_ui.h"

#include "esp_log.h"

#include "lv_port_indev.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_BLE_DETECT";

typedef struct {
  const char *name;
  const char *icon;
  int target;
} ui_ble_detect_item_t;

static const ui_ble_detect_item_t MENU_ITEMS[] = {
    {"Scan Devices", "/assets/icons/bluetooth_searching.bin", SCREEN_BLE_SCAN},
    {"Sniffer", "/assets/icons/monitoring.bin", SCREEN_BLE_SNIFFER},
    {"Tracker Detector", "/assets/icons/troubleshoot.bin", SCREEN_BLE_TRACKER},
    {"Skimmer Detector", "/assets/icons/warning.bin", SCREEN_BLE_SKIMMER},
    {"Exposure Beacons", "/assets/icons/broadcast_on_personal.bin", SCREEN_BLE_EXPOSURE},
    {"GATT Explorer", "/assets/icons/hub.bin", SCREEN_GATT_EXPLORER},
    {"Track Device", "/assets/icons/sensors.bin", SCREEN_BLE_TRACK_DEVICE},
};
#define MENU_ITEMS_COUNT (sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static void ble_detect_input(const input_event_t *ev, void *ctx);

void ui_ble_detect_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "DETECT", "/assets/icons/bluetooth_searching.bin");
  for (int i = 0; i < (int)MENU_ITEMS_COUNT; i++) {
    menu_component_add_item(&s_menu, MENU_ITEMS[i].icon, MENU_ITEMS[i].name);
  }

  ui_input_set_screen_handler(ble_detect_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
  ESP_LOGI(TAG, "BLE detect menu opened");
}

static void ble_detect_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_BLE_MENU);
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0 && sel < (int)MENU_ITEMS_COUNT && MENU_ITEMS[sel].target >= 0) {
          ui_switch_screen(MENU_ITEMS[sel].target);
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
