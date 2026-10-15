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

#include "ble_hid_keyboard.h"

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_protocol.h"

static const char *TAG = "BLE_HID_KEYBOARD";

#define HID_SPI_INIT_TIMEOUT_MS       15000
#define HID_SPI_DEINIT_TIMEOUT_MS     2000
#define HID_SPI_QUERY_TIMEOUT_MS      2000
#define HID_SPI_SEND_KEY_TIMEOUT_MS   1000
#define HID_SPI_SEND_MOUSE_TIMEOUT_MS 1000

static bool s_is_connected = false;

esp_err_t ble_hid_init(void) {
  esp_err_t ret =
      spi_bridge_send_command(SPI_ID_BT_HID_INIT, NULL, 0, NULL, NULL, 0, HID_SPI_INIT_TIMEOUT_MS);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init HID on C5: %s", esp_err_to_name(ret));
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "HID Keyboard initialized on C5");
  return ESP_OK;
}

esp_err_t ble_hid_deinit(void) {
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_BT_HID_DEINIT, NULL, 0, NULL, NULL, 0, HID_SPI_DEINIT_TIMEOUT_MS);

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to deinit HID on C5: %s", esp_err_to_name(ret));
  }

  s_is_connected = false;
  return ESP_OK;
}

bool ble_hid_is_connected(void) {
  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_HID_IS_CONNECTED,
                                          NULL,
                                          0,
                                          &resp_hdr,
                                          resp_buf,
                                          sizeof(resp_buf),
                                          HID_SPI_QUERY_TIMEOUT_MS);

  if (ret != ESP_OK || resp_hdr.length < 1) {
    return false;
  }

  s_is_connected = (resp_buf[0] != 0);
  return s_is_connected;
}

void ble_hid_send_key(uint8_t keycode, uint8_t modifier) {
  uint8_t payload[2] = {modifier, keycode};

  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  spi_bridge_send_command(SPI_ID_BT_HID_SEND_KEY,
                          payload,
                          sizeof(payload),
                          &resp_hdr,
                          resp_buf,
                          sizeof(resp_buf),
                          HID_SPI_SEND_KEY_TIMEOUT_MS);
}

void ble_hid_send_mouse(int8_t x, int8_t y, uint8_t buttons, int8_t wheel) {
  uint8_t payload[4] = {buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel};
  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_HID_SEND_MOUSE,
                                          payload,
                                          sizeof(payload),
                                          NULL,
                                          NULL,
                                          0,
                                          HID_SPI_SEND_MOUSE_TIMEOUT_MS);
  if (ret != ESP_OK)
    ESP_LOGW(TAG, "Failed to send HID mouse report: %s", esp_err_to_name(ret));
}
