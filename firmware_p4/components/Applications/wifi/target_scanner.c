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

#include "target_scanner.h"

#include <string.h>

#include "esp_log.h"

#include "led_control.h"
#include "spi_bridge.h"

static const char *TAG = "TARGET_SCANNER";

#define TARGET_SCANNER_MAX_RESULTS 200

static target_scanner_record_t s_cached_results[TARGET_SCANNER_MAX_RESULTS];
static uint16_t s_cached_count = 0;

static bool fetch_results(uint16_t count) {
  if (count == 0) {
    s_cached_count = 0;
    return true;
  }
  if (count > TARGET_SCANNER_MAX_RESULTS) {
    count = TARGET_SCANNER_MAX_RESULTS;
  }
  spi_header_t resp;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t idx = i;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                                (uint8_t *)&idx,
                                2,
                                &resp,
                                (uint8_t *)&s_cached_results[i],
                                sizeof(target_scanner_record_t)) != ESP_OK) {
      return false;
    }
  }
  s_cached_count = count;
  return true;
}

bool target_scanner_start(const uint8_t *target_bssid, uint8_t channel) {
  if (target_bssid == NULL)
    return false;
  uint8_t payload[7];
  memcpy(payload, target_bssid, 6);
  payload[6] = channel;
  esp_err_t err = spi_bridge_send_command(
      SPI_ID_WIFI_TARGET_SCAN_START, payload, sizeof(payload), NULL, NULL, 2000);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Target scan start failed over SPI");
    led_signal_error();
    return false;
  }
  s_cached_count = 0;
  return true;
}

target_scanner_record_t *target_scanner_get_results(uint16_t *out_count) {
  bool is_scanning = false;
  uint16_t live_count = 0;
  target_scanner_record_t *results = target_scanner_get_live_results(&live_count, &is_scanning);
  if (is_scanning)
    return NULL;
  if (out_count != NULL)
    *out_count = live_count;
  return results;
}

target_scanner_record_t *target_scanner_get_live_results(uint16_t *out_count, bool *is_scanning) {
  spi_header_t resp;
  uint8_t payload[3] = {0};
  if (spi_bridge_send_command(
          SPI_ID_WIFI_TARGET_SCAN_STATUS, NULL, 0, &resp, payload, sizeof(payload)) != ESP_OK) {
    if (out_count != NULL)
      *out_count = 0;
    if (is_scanning != NULL)
      *is_scanning = false;
    return NULL;
  }
  bool scanning = (payload[0] != 0);
  uint16_t total = 0;
  memcpy(&total, payload + 1, sizeof(uint16_t));
  if (is_scanning != NULL)
    *is_scanning = scanning;

  if (!fetch_results(total)) {
    if (out_count != NULL)
      *out_count = 0;
    return NULL;
  }

  if (out_count != NULL)
    *out_count = s_cached_count;
  return (s_cached_count > 0) ? s_cached_results : NULL;
}

void target_scanner_free_results(void) {
  spi_bridge_send_command(SPI_ID_WIFI_TARGET_FREE, NULL, 0, NULL, NULL, 2000);
  s_cached_count = 0;
  memset(s_cached_results, 0, sizeof(s_cached_results));
}

bool target_scanner_save_results_to_internal_flash(void) {
  return (spi_bridge_send_command(SPI_ID_WIFI_TARGET_SAVE_FLASH, NULL, 0, NULL, NULL, 5000) ==
          ESP_OK);
}

bool target_scanner_save_results_to_sd_card(void) {
  return (spi_bridge_send_command(SPI_ID_WIFI_TARGET_SAVE_SD, NULL, 0, NULL, NULL, 5000) == ESP_OK);
}
