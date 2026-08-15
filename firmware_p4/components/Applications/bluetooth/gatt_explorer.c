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

#include "gatt_explorer.h"

#include <string.h>

#include "esp_log.h"

#include "bluetooth_service.h"
#include "spi_bridge.h"
#include "spi_protocol.h"

static const char *TAG = "GATT_EXPLORER";

#define GATT_ADDR_PAYLOAD_SIZE 7
#define GATT_SPI_TIMEOUT_MS    10000

static bool s_is_busy = false;

bool gatt_explorer_start(const uint8_t *addr, uint8_t addr_type) {
  if (s_is_busy)
    return false;

  if (!bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "Bluetooth service not running");
    return false;
  }

  uint8_t payload[GATT_ADDR_PAYLOAD_SIZE];
  memcpy(payload, addr, 6);
  payload[6] = addr_type;

  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_BT_APP_GATT_EXP, payload, sizeof(payload), &resp_hdr, resp_buf, sizeof(resp_buf), GATT_SPI_TIMEOUT_MS);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    ESP_LOGE(TAG, "Failed to start GATT exploration on C5");
    return false;
  }

  s_is_busy = true;
  ESP_LOGI(TAG, "GATT exploration started on C5");
  return true;
}

bool gatt_explorer_is_busy(void) {
  return s_is_busy;
}
