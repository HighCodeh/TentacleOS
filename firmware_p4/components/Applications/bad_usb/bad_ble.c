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

#include "bad_ble.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble_hid_keyboard.h"
#include "hid_hal.h"
#include "resource_mgr.h"

static const char *TAG = "BAD_BLE";

#define BLE_CONNECT_TIMEOUT_MS 30000
#define BLE_POLL_INTERVAL_MS   200

static bool s_is_initialized = false;
static res_handle_t s_ble_res = RES_HANDLE_NONE;

esp_err_t bad_ble_init(void) {
  if (s_is_initialized) {
    ESP_LOGW(TAG, "Already initialized");
    return ESP_ERR_INVALID_STATE;
  }

  res_request_t res_req = {.id = RES_BLE,
                           .lane = RES_LANE_MAIN,
                           .owner_kind = RES_OWNER_UI,
                           .owner_task = NULL,
                           .allow_preempt = false};
  if (resource_acquire(&res_req, &s_ble_res) != ESP_OK) {
    ESP_LOGW(TAG, "BLE host busy; BadBLE not started");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = ble_hid_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start BLE HID: %s", esp_err_to_name(err));
    resource_release(s_ble_res);
    s_ble_res = RES_HANDLE_NONE;
    return err;
  }

  hid_hal_register_callback(ble_hid_send_key, NULL, bad_ble_wait_for_connection, NULL);
  s_is_initialized = true;

  ESP_LOGI(TAG, "Initialized");
  return ESP_OK;
}

esp_err_t bad_ble_deinit(void) {
  if (!s_is_initialized) {
    ESP_LOGW(TAG, "Not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  hid_hal_register_callback(NULL, NULL, NULL, NULL);
  ble_hid_deinit();

  s_is_initialized = false;
  resource_release(s_ble_res);
  s_ble_res = RES_HANDLE_NONE;
  ESP_LOGI(TAG, "Deinitialized");
  return ESP_OK;
}

bool bad_ble_wait_for_connection_ex(bad_ble_abort_cb_t should_abort) {
  ESP_LOGI(TAG, "Waiting for Bluetooth host to pair...");
  int waited_ms = 0;
  while (!ble_hid_is_connected()) {
    if (should_abort != NULL && should_abort()) {
      ESP_LOGW(TAG, "Wait for connection aborted");
      return false;
    }
    if (waited_ms >= BLE_CONNECT_TIMEOUT_MS) {
      ESP_LOGE(TAG, "No Bluetooth host after %d ms", BLE_CONNECT_TIMEOUT_MS);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(BLE_POLL_INTERVAL_MS));
    waited_ms += BLE_POLL_INTERVAL_MS;
  }
  ESP_LOGI(TAG, "Bluetooth host connected");
  return true;
}

void bad_ble_wait_for_connection(void) {
  (void)bad_ble_wait_for_connection_ex(NULL);
}
