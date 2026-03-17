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

#include "ble_hid_keyboard.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "esp_log.h"

static const char *TAG = "BLE_HID";

static bool is_connected = false;

esp_err_t ble_hid_init(void) {
  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_HID_INIT,
      NULL, 0,
      &resp_hdr, resp_buf, 5000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    ESP_LOGE(TAG, "Failed to init HID on C5");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "HID Keyboard initialized on C5");
  return ESP_OK;
}

esp_err_t ble_hid_deinit(void) {
  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_HID_DEINIT,
      NULL, 0,
      &resp_hdr, resp_buf, 2000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    ESP_LOGW(TAG, "Failed to deinit HID on C5");
  }

  is_connected = false;
  return ESP_OK;
}

bool ble_hid_is_connected(void) {
  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_HID_IS_CONNECTED,
      NULL, 0,
      &resp_hdr, resp_buf, 2000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    return false;
  }

  is_connected = resp_buf[1];
  return is_connected;
}

void ble_hid_send_key(uint8_t keycode, uint8_t modifier) {
  // Payload: modifier[1] + keycode[1]
  uint8_t payload[2] = { modifier, keycode };

  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  spi_bridge_send_command(SPI_ID_BT_HID_SEND_KEY,
      payload, sizeof(payload),
      &resp_hdr, resp_buf, 1000);
}
