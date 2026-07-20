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

#include "ble_scan_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "ui_feedback.h"

static const char *TAG = "BLE_SCAN_UI";

#define NAV_TIMER_MS          50
#define SCAN_RESULT_COLOR_HEX 0x00E676
#define BLE_MAX_DEVS          12
#define SCAN_SIM_STEPS        4
#define SCAN_SIM_STEP_MS      220
#define BLE_DEV_ICON          "/assets/icons/radar_icon.bin"
#define BLE_DEV_LABEL_LEN     32
#define BLE_SCAN_TASK_STACK   4096
#define BLE_SCAN_TASK_PRIO    4

typedef enum { SCAN_RUNNING, SCAN_DONE, SCAN_FAIL } scan_state_t;

typedef struct {
  char name[20];
  int8_t rssi;
} mock_ble_dev_t;

static const mock_ble_dev_t MOCK_BLE_DEVS[] = {
    {"Galaxy Buds", -51},
    {"Mi Band 7", -60},
    {"JBL Flip 6", -44},
    {"AirPods", -66},
    {"Tile Tracker", -73},
};
#define MOCK_BLE_DEV_COUNT (sizeof(MOCK_BLE_DEVS) / sizeof(MOCK_BLE_DEVS[0]))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static scan_state_t s_scan_state = SCAN_RUNNING;
static bool s_scanning = false;
static int s_dev_count = 0;
static char s_dev_labels[BLE_MAX_DEVS][BLE_DEV_LABEL_LEN];

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *t);

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "BLE Devices", BLE_DEV_ICON);

  if (s_scan_state == SCAN_RUNNING) {
    menu_component_add_item(&s_menu, BLE_DEV_ICON, "Scanning...");
  } else if (s_scan_state == SCAN_FAIL) {
    menu_component_add_item(&s_menu, BLE_DEV_ICON, "Scan failed (C5?)");
  } else if (s_dev_count == 0) {
    menu_component_add_item(&s_menu, BLE_DEV_ICON, "No devices found");
  } else {
    for (int i = 0; i < s_dev_count; i++) {
      menu_component_add_item(&s_menu, BLE_DEV_ICON, s_dev_labels[i]);
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(SCAN_RESULT_COLOR_HEX));
    }
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_BLE_SCAN)
    return;
  build_screen();
  if (s_scan_state == SCAN_DONE && s_dev_count > 0)
    ui_feedback(UI_FB_READ);
  ESP_LOGI(TAG, "scan finished: state=%d, %d device(s)", (int)s_scan_state, s_dev_count);
}

static void ble_scan_task(void *arg) {
  (void)arg;
  int count = 0;

  for (int i = 0; i < SCAN_SIM_STEPS; i++)
    vTaskDelay(pdMS_TO_TICKS(SCAN_SIM_STEP_MS));

  for (size_t i = 0; i < MOCK_BLE_DEV_COUNT && count < BLE_MAX_DEVS; i++) {
    snprintf(s_dev_labels[count],
             sizeof(s_dev_labels[count]),
             "%.20s (%d)",
             MOCK_BLE_DEVS[i].name,
             MOCK_BLE_DEVS[i].rssi);
    count++;
  }

  s_dev_count = count;
  s_scan_state = SCAN_DONE;
  s_scanning = false;
  lv_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
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

  if (down && !s_btn_down_last)
    menu_component_next(&s_menu);
  if (up && !s_btn_up_last)
    menu_component_prev(&s_menu);

  if ((back && !s_btn_back_last) || (left && !s_btn_left_last))
    ui_switch_screen(SCREEN_BLE_MENU);

  if (((ok && !s_btn_ok_last) || (right && !s_btn_right_last)) && !s_scanning) {
    ui_ble_scan_open();
    return;
  }

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

void ui_ble_scan_open(void) {
  s_scan_state = SCAN_RUNNING;
  s_dev_count = 0;
  build_screen();

  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreate(
            ble_scan_task, "ble_scan", BLE_SCAN_TASK_STACK, NULL, BLE_SCAN_TASK_PRIO, NULL) !=
        pdPASS) {
      s_scanning = false;
      s_scan_state = SCAN_FAIL;
      build_screen();
    }
  }

  ESP_LOGI(TAG, "BLE scan screen opened (mock scan)");
}
