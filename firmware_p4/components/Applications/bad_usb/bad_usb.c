// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "bad_usb.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "tusb_desc.h"

#include "hid_hal.h"

static const char *TAG = "BAD_USB";

#define HID_REPORT_ID_KEYBOARD 1
#define HID_REPORT_ID_MOUSE    2
#define USB_POLL_INTERVAL_MS   100
#define USB_SETTLE_DELAY_MS    2000

static bool s_is_initialized = false;

static void send_keyboard_report(uint8_t keycode, uint8_t modifier) {
  uint8_t keycode_array[6] = {0};
  keycode_array[0] = keycode;
  tud_hid_keyboard_report(HID_REPORT_ID_KEYBOARD, modifier, keycode_array);
}

static void send_mouse_report(int8_t x, int8_t y, uint8_t buttons, int8_t wheel) {
  tud_hid_mouse_report(HID_REPORT_ID_MOUSE, buttons, x, y, wheel, 0);
}

void bad_usb_wait_for_connection(void) {
  ESP_LOGI(TAG, "Waiting for USB host connection...");
  while (!tud_mounted()) {
    vTaskDelay(pdMS_TO_TICKS(USB_POLL_INTERVAL_MS));
  }
  ESP_LOGI(TAG, "USB host connected. Settling for %d ms...", USB_SETTLE_DELAY_MS);
  vTaskDelay(pdMS_TO_TICKS(USB_SETTLE_DELAY_MS));
}

esp_err_t bad_usb_init(void) {
  if (s_is_initialized) {
    ESP_LOGW(TAG, "Already initialized");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = busb_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize TinyUSB: %s", esp_err_to_name(err));
    return err;
  }

  hid_hal_register_callback(send_keyboard_report, send_mouse_report, bad_usb_wait_for_connection);
  s_is_initialized = true;

  ESP_LOGI(TAG, "Initialized");
  return ESP_OK;
}

esp_err_t bad_usb_deinit(void) {
  if (!s_is_initialized) {
    ESP_LOGW(TAG, "Not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Deinitializing...");
  hid_hal_register_callback(NULL, NULL, NULL);

  esp_err_t err = tinyusb_driver_uninstall();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to uninstall TinyUSB driver: %s", esp_err_to_name(err));
    return err;
  }

  s_is_initialized = false;
  ESP_LOGI(TAG, "Deinitialized");
  return ESP_OK;
}
