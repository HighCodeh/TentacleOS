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

#include "bridge_manager.h"

#include <string.h>

#include "esp_log.h"

#include "c5_flasher.h"
#include "ota_version.h"
#include "spi_bridge.h"

static const char *TAG = "BRIDGE_MGR";

#define VERSION_BUF_SIZE   32
#define VERSION_TIMEOUT_MS 1000

esp_err_t bridge_manager_init(void) {
  ESP_LOGI(TAG, "Initializing bridge manager");
  ESP_LOGI(TAG, "Expected C5 version: %s", FIRMWARE_VERSION);

  // The HighBoy V2 PCB has no bridge IRQ trace (GPIO_BRIDGE_IRQ_PIN = -1), so
  // the master polls the bus instead of waiting on an IRQ. Must match the C5.
  if (spi_bridge_master_init_mode(SPI_BRIDGE_MODE_POLL) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init SPI bridge");
    return ESP_FAIL;
  }

  spi_header_t resp_header;
  uint8_t resp_ver[VERSION_BUF_SIZE];
  memset(resp_ver, 0, sizeof(resp_ver));

  ESP_LOGI(TAG, "Checking C5 version");
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_SYSTEM_VERSION, NULL, 0, &resp_header, resp_ver, VERSION_TIMEOUT_MS);

  bool is_update_needed = false;

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "C5 not responding, assuming recovery needed");
    is_update_needed = true;
  } else {
    ESP_LOGI(TAG, "C5 version: %s (expected: %s)", resp_ver, FIRMWARE_VERSION);
    if (strcmp((char *)resp_ver, FIRMWARE_VERSION) != 0) {
      is_update_needed = true;
    }
  }

  if (is_update_needed) {
    ESP_LOGW(TAG, "C5 update required");
    c5_flasher_init();
    if (c5_flasher_update(NULL, 0) == ESP_OK) {
      ESP_LOGI(TAG, "C5 firmware uploaded");
    } else {
      ESP_LOGE(TAG, "C5 synchronization failed");
      spi_bridge_set_alive(false);
      return ESP_FAIL;
    }
  } else {
    ESP_LOGI(TAG, "C5 is up to date");
  }

  // Final probe: confirm the SPI bridge is actually responding before
  // letting background tasks poll it
  memset(resp_ver, 0, sizeof(resp_ver));
  ret = spi_bridge_send_command(
      SPI_ID_SYSTEM_VERSION, NULL, 0, &resp_header, resp_ver, VERSION_TIMEOUT_MS);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "C5 SPI bridge not responding — disabling bridge polling");
    spi_bridge_set_alive(false);
    return ESP_OK;
  }

  ESP_LOGI(TAG, "C5 bridge alive");
  return ESP_OK;
}

esp_err_t bridge_manager_force_update(void) {
  c5_flasher_init();
  return c5_flasher_update(NULL, 0);
}
