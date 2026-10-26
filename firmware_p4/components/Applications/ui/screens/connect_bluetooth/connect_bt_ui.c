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

// Copyright (c) 2025 HIGH CODE LLC

#include "connect_bt_ui.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble_scanner.h"
#include "bluetooth_service.h"
#include "esp_log.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "sys_prio.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "CONNECT_BT_UI";

#define BT_MAX_DEVICES        12
#define BT_SCAN_TIMEOUT_MS    15000
#define BT_SCAN_POLL_MS       150
#define BT_CONNECT_TIMEOUT_MS 32000
#define BT_TASK_STACK         6144

typedef enum { BT_SCANNING, BT_READY, BT_SCAN_FAILED } bt_state_t;

typedef struct {
  char name[32];
  uint8_t addr[6];
  uint8_t addr_type;
  int8_t rssi;
} bt_device_t;

static lv_obj_t *s_screen;
static menu_component_t s_menu;
static bt_device_t s_devices[BT_MAX_DEVICES];
static volatile bt_state_t s_state;
static volatile bool s_scanning;
static volatile bool s_connecting;
static int s_count;
static int s_connect_index;
static esp_err_t s_connect_result;

static void connect_bt_input(const input_event_t *ev, void *ctx);
static void build_screen(void);

static void scan_done_cb(void *arg) {
  (void)arg;
  if (ui_current_screen() == SCREEN_CONNECT_BLUETOOTH)
    build_screen();
}

static void scan_task(void *arg) {
  (void)arg;
  s_count = 0;
  if (!ble_scanner_start()) {
    s_state = BT_SCAN_FAILED;
    s_scanning = false;
    ui_async_call(scan_done_cb, NULL);
    vTaskDelete(NULL);
    return;
  }

  uint32_t waited = 0;
  while (ble_scanner_is_scanning() && waited < BT_SCAN_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(BT_SCAN_POLL_MS));
    waited += BT_SCAN_POLL_MS;
  }

  uint16_t n = 0;
  bluetooth_service_scan_result_t *results = ble_scanner_get_results(&n);
  if (!ble_scanner_is_scanning()) {
    for (uint16_t i = 0; i < n && s_count < BT_MAX_DEVICES; i++) {
      bt_device_t *dst = &s_devices[s_count++];
      snprintf(dst->name,
               sizeof(dst->name),
               "%.31s",
               results[i].name[0] ? results[i].name : "(unnamed)");
      memcpy(dst->addr, results[i].addr, sizeof(dst->addr));
      dst->addr_type = results[i].addr_type;
      dst->rssi = (int8_t)results[i].rssi;
    }
    s_state = BT_READY;
  } else {
    s_state = BT_SCAN_FAILED;
  }
  ble_scanner_free_results();
  s_scanning = false;
  ui_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void connect_done_cb(void *arg) {
  (void)arg;
  if (ui_current_screen() != SCREEN_CONNECT_BLUETOOTH)
    return;
  if (s_connect_result == ESP_OK) {
    ui_feedback(UI_FB_READ);
    notify(NOTIFY_INFO, "BLE device connected");
  } else if (s_connect_result == ESP_ERR_TIMEOUT) {
    notify(NOTIFY_WARNING, "BLE connection timed out");
  } else {
    notify(NOTIFY_WARNING, "BLE connection failed");
  }
}

static void connect_task(void *arg) {
  (void)arg;
  const bt_device_t target = s_devices[s_connect_index];
  int before = bluetooth_service_get_connected_count();
  esp_err_t err = bluetooth_service_connect(target.addr, target.addr_type, NULL);
  uint32_t waited = 0;
  while (err == ESP_OK && bluetooth_service_get_connected_count() <= before &&
         waited < BT_CONNECT_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(250));
    waited += 250;
  }
  if (err == ESP_OK && bluetooth_service_get_connected_count() <= before)
    err = ESP_ERR_TIMEOUT;
  s_connect_result = err;
  s_connecting = false;
  ui_async_call(connect_done_cb, NULL);
  vTaskDelete(NULL);
}

static void begin_scan(void) {
  if (s_scanning || s_connecting)
    return;
  s_state = BT_SCANNING;
  s_scanning = true;
  build_screen();
  if (xTaskCreatePinnedToCore(scan_task,
                              "bt_connect_scan",
                              BT_TASK_STACK,
                              NULL,
                              SYS_PRIO_SERVICE_LO,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    s_scanning = false;
    s_state = BT_SCAN_FAILED;
    build_screen();
  }
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "BLE DEVICES", "/assets/icons/bluetooth.bin");
  if (s_state == BT_SCANNING) {
    menu_component_add_item(&s_menu, "/assets/icons/bluetooth_searching.bin", "Scanning on C5...");
    menu_component_set_hint(&s_menu, "BACK Cancel");
  } else if (s_state == BT_SCAN_FAILED) {
    menu_component_add_item(
        &s_menu, "/assets/icons/bluetooth_searching.bin", "Scan failed / no response");
    menu_component_set_hint(&s_menu, "RIGHT Rescan  BACK");
  } else if (s_count == 0) {
    menu_component_add_item(&s_menu, "/assets/icons/bluetooth_searching.bin", "No devices found");
    menu_component_set_hint(&s_menu, "RIGHT Rescan  BACK");
  } else {
    char label[48];
    for (int i = 0; i < s_count; i++) {
      snprintf(label, sizeof(label), "%.31s  %d dBm", s_devices[i].name, s_devices[i].rssi);
      menu_component_add_item(&s_menu, "/assets/icons/bluetooth.bin", label);
    }
    menu_component_set_hint(&s_menu, "OK Connect  RIGHT Rescan  BACK");
  }
  ui_input_set_screen_handler(connect_bt_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void connect_bt_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  bool press = ev->action == INPUT_ACTION_PRESS;
  bool nav = press || ev->action == INPUT_ACTION_REPEAT;
  if (ev->button == INPUT_BTN_BACK && press) {
    ui_switch_screen(SCREEN_CONNECTION_SETTINGS);
    return;
  }
  if (s_state != BT_READY || s_count == 0) {
    if (ev->button == INPUT_BTN_RIGHT && press)
      begin_scan();
    return;
  }
  if (ev->button == INPUT_BTN_DOWN && nav)
    menu_component_next(&s_menu);
  else if (ev->button == INPUT_BTN_UP && nav)
    menu_component_prev(&s_menu);
  else if (ev->button == INPUT_BTN_RIGHT && press)
    begin_scan();
  else if (ev->button == INPUT_BTN_OK && press && !s_connecting) {
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0 && sel < s_count) {
      s_connect_index = sel;
      s_connecting = true;
      notify(NOTIFY_INFO, "Connecting over BLE...");
      if (xTaskCreatePinnedToCore(connect_task,
                                  "bt_connect",
                                  BT_TASK_STACK,
                                  NULL,
                                  SYS_PRIO_SERVICE_LO,
                                  NULL,
                                  SYS_CORE_RADIO) != pdPASS) {
        s_connecting = false;
        notify(NOTIFY_WARNING, "No memory for BLE task");
      }
    }
  }
}

void ui_connect_bt_open(void) {
  s_count = 0;
  s_state = BT_SCANNING;
  s_scanning = false;
  s_connecting = false;
  build_screen();
  begin_scan();
  ESP_LOGI(TAG, "BLE connection screen opened (real C5 scan)");
}
