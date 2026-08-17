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
#define USB_MOUNT_TIMEOUT_MS   8000

static bool s_is_initialized = false;

static void send_keyboard_report(uint8_t keycode, uint8_t modifier);
static void send_mouse_report(int8_t x, int8_t y, uint8_t buttons, int8_t wheel);

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

bool bad_usb_wait_for_connection_ex(bad_usb_abort_cb_t should_abort) {
  ESP_LOGI(TAG, "Waiting for USB host connection...");
  int waited_ms = 0;
  while (!tud_mounted()) {
    if (should_abort != NULL && should_abort()) {
      ESP_LOGW(TAG, "Wait for connection aborted");
      return false;
    }
    if (waited_ms >= USB_MOUNT_TIMEOUT_MS) {
      ESP_LOGE(TAG, "USB host not detected after %d ms", USB_MOUNT_TIMEOUT_MS);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(USB_POLL_INTERVAL_MS));
    waited_ms += USB_POLL_INTERVAL_MS;
  }
  ESP_LOGI(TAG, "USB host connected. Settling for %d ms...", USB_SETTLE_DELAY_MS);
  for (int elapsed = 0; elapsed < USB_SETTLE_DELAY_MS; elapsed += USB_POLL_INTERVAL_MS) {
    if (should_abort != NULL && should_abort()) {
      ESP_LOGW(TAG, "Settle aborted");
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(USB_POLL_INTERVAL_MS));
  }
  return true;
}

void bad_usb_wait_for_connection(void) {
  (void)bad_usb_wait_for_connection_ex(NULL);
}

static void send_keyboard_report(uint8_t keycode, uint8_t modifier) {
  uint8_t keycode_array[6] = {0};
  keycode_array[0] = keycode;
  tud_hid_keyboard_report(HID_REPORT_ID_KEYBOARD, modifier, keycode_array);
}

static void send_mouse_report(int8_t x, int8_t y, uint8_t buttons, int8_t wheel) {
  tud_hid_mouse_report(HID_REPORT_ID_MOUSE, buttons, x, y, wheel, 0);
}
