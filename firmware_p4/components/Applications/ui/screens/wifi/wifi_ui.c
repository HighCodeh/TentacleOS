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

#include "wifi_ui.h"

#include "esp_log.h"

#include "lv_port_indev.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_WIFI_MENU";


static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static const struct {
  const char *name;
  const char *icon;
  int target;
} MENU_ITEMS[] = {
    {"SCAN NETWORKS", "/assets/icons/wifi_find.bin", SCREEN_WIFI_SCAN_MENU},
    {"EDIT NETWORKS", "/assets/icons/edit.bin", SCREEN_WIFI_NAMES},
    {"CHANNEL ANALYSIS", "/assets/icons/graphic_eq.bin", SCREEN_WIFI_CHANNELS},
    {"SCAN CLIENTS", "/assets/icons/devices.bin", SCREEN_WIFI_CLIENTS},
    {"ATTACKS", "/assets/icons/bolt.bin", SCREEN_WIFI_ATTACK_MENU},
    {"PACKETS CAPTURE", "/assets/icons/download.bin", SCREEN_WIFI_PACKETS_MENU},
    {"EVIL-TWIN", "/assets/icons/wifi_tethering.bin", SCREEN_WIFI_EVIL_TWIN},
    {"PORT SCANNER", "/assets/icons/troubleshoot.bin", SCREEN_WIFI_PORT_SCAN},
    {"PROBE MONITOR", "/assets/icons/wifi_find.bin", SCREEN_WIFI_PROBE_MON},
    {"TARGET CLIENTS", "/assets/icons/devices.bin", SCREEN_WIFI_TARGET_CLIENTS},
    {"DEAUTH DETECTOR", "/assets/icons/monitoring.bin", SCREEN_WIFI_DEAUTH_DETECTOR},
    {"SIGNAL LOCATOR", "/assets/icons/graphic_eq.bin", SCREEN_WIFI_SIGNAL_LOCATOR},
    {"HANDSHAKE / PMKID", "/assets/icons/download.bin", SCREEN_WIFI_HANDSHAKE},
    {"HOTSPOT (AP)", "/assets/icons/wifi_tethering.bin", SCREEN_WIFI_HOTSPOT},
};

#define MENU_ITEMS_COUNT (sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]))


static void wifi_menu_input(const input_event_t *ev, void *ctx) {
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
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0 && sel < (int)MENU_ITEMS_COUNT)
          ui_switch_screen(MENU_ITEMS[sel].target);
      }
      break;
    default:
      break;
  }
}

void ui_wifi_menu_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "WIFI", "/assets/icons/wifi.bin");

  for (int i = 0; i < (int)MENU_ITEMS_COUNT; i++) {
    menu_component_add_item(&s_menu, MENU_ITEMS[i].icon, MENU_ITEMS[i].name);
  }

  ui_input_set_screen_handler(wifi_menu_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
