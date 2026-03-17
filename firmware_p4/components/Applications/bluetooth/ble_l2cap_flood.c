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

#include "ble_l2cap_flood.h"
#include "bluetooth_service.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "L2CAP_FLOOD";

static bool is_running = false;

esp_err_t ble_l2cap_flood_start(const uint8_t *addr, uint8_t addr_type) {
  if (is_running) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "Bluetooth service not running");
    return ESP_FAIL;
  }

  // Payload: addr[6] + addr_type[1]
  uint8_t payload[7];
  memcpy(payload, addr, 6);
  payload[6] = addr_type;

  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_APP_FLOOD,
      payload, sizeof(payload),
      &resp_hdr, resp_buf, 5000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    ESP_LOGE(TAG, "Failed to start L2CAP flood on C5");
    return ESP_FAIL;
  }

  is_running = true;
  ESP_LOGI(TAG, "L2CAP Flood started");
  return ESP_OK;
}

esp_err_t ble_l2cap_flood_stop(void) {
  if (!is_running) return ESP_OK;

  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_APP_STOP,
      NULL, 0,
      &resp_hdr, resp_buf, 2000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    ESP_LOGW(TAG, "Failed to stop L2CAP flood on C5");
  }

  is_running = false;
  ESP_LOGI(TAG, "L2CAP Flood stopped");
  return ESP_OK;
}

bool ble_l2cap_flood_is_running(void) {
  return is_running;
}
