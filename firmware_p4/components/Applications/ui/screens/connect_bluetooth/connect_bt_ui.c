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

#include "connect_bt_ui.h"

#include <stdio.h>

#include "esp_log.h"

#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "CONNECT_BT_UI";

#define PAIRED_DEVICE_COLOR_HEX 0x00E676

typedef struct {
  const char *name;
  bool is_paired;
} bt_device_t;

static const bt_device_t MOCK_DEVICES[] = {
    {"PIXEL_BUDS_PRO", true},
    {"MECHANICAL_KB", true},
    {"UNKNOWN_PHONE", false},
    {"SMART_WATCH_X", false},
};
#define MOCK_DEVICES_COUNT ((int)(sizeof(MOCK_DEVICES) / sizeof(MOCK_DEVICES[0])))

static const char *const DEVICE_ICONS[] = {
    "/assets/icons/earbuds.bin",
    "/assets/icons/keyboard.bin",
    "/assets/icons/smartphone.bin",
    "/assets/icons/watch.bin",
};

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static void connect_bt_input(const input_event_t *ev, void *ctx) {
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
    case INPUT_BTN_RIGHT:
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_CONNECTION_SETTINGS);
      break;
    default:
      break;
  }
}

void ui_connect_bt_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "DEVICES", "/assets/icons/bluetooth.bin");

  for (int i = 0; i < MOCK_DEVICES_COUNT; i++) {
    menu_component_add_item(&s_menu, DEVICE_ICONS[i], MOCK_DEVICES[i].name);

    if (MOCK_DEVICES[i].is_paired)
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(PAIRED_DEVICE_COLOR_HEX));
  }

  ui_input_set_screen_handler(connect_bt_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
  ESP_LOGI(TAG, "BT device list opened (%d device(s))", MOCK_DEVICES_COUNT);
}
