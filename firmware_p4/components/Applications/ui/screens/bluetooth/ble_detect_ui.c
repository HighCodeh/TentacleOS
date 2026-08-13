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

#include "buttons_gpio.h"
#include "lv_port_indev.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "UI_BLE_DETECT";

#define NAV_TIMER_INTERVAL_MS 50

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
static lv_timer_t *s_nav_timer = NULL;
static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *t);

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

  if (s_nav_timer == NULL) {
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);
  }

  ui_screen_load(s_screen);
  ESP_LOGI(TAG, "BLE detect menu opened");
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }

  if (ui_input_is_locked()) {
    return;
  }

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (down && !s_btn_down_last) {
    menu_component_next(&s_menu);
  }

  if (up && !s_btn_up_last) {
    menu_component_prev(&s_menu);
  }

  if ((back && !s_btn_back_last) || (left && !s_btn_left_last)) {
    ui_switch_screen(SCREEN_BLE_MENU);
  }

  if ((ok && !s_btn_ok_last) || (right && !s_btn_right_last)) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0 && sel < (int)MENU_ITEMS_COUNT && MENU_ITEMS[sel].target >= 0) {
      ui_switch_screen(MENU_ITEMS[sel].target);
    }
  }

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}
