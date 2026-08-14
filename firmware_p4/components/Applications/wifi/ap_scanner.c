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

#include "ap_scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "cJSON.h"
#include "spi_bridge.h"
#include "storage_write.h"
#include "tos_storage_paths.h"

static const char *TAG = "AP_SCANNER";

// Scan results are persisted on the P4 SD card only. Without a card they are
// shown on screen but not saved (no flash fallback). Latest scan overwrites.
#define AP_SCAN_SAVE_PATH TOS_PATH_WIFI "/scanned_aps.json"

static wifi_ap_record_t *s_cached_results = NULL;
static uint16_t s_cached_count = 0;
static bool s_is_scan_ready = false;
static wifi_ap_record_t s_empty_record;

static bool fetch_results(void) {
  spi_header_t resp;
  uint8_t payload[2];
  uint16_t magic_count = SPI_DATA_INDEX_COUNT;

  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_DATA, (uint8_t *)&magic_count, 2, &resp, payload, 1000) != ESP_OK) {
    return false;
  }

  uint16_t count = 0;
  memcpy(&count, payload, 2);

  if (s_cached_results != NULL) {
    free(s_cached_results);
    s_cached_results = NULL;
  }
  s_cached_count = 0;

  if (count == 0) {
    s_is_scan_ready = true;
    return true;
  }

  s_cached_results = (wifi_ap_record_t *)malloc(count * sizeof(wifi_ap_record_t));
  if (s_cached_results == NULL) {
    ESP_LOGW(TAG, "Failed to allocate AP results buffer");
    return false;
  }

  for (uint16_t i = 0; i < count; i++) {
    if (spi_bridge_send_command(
            SPI_ID_SYSTEM_DATA, (uint8_t *)&i, 2, &resp, (uint8_t *)&s_cached_results[i], 1000) !=
        ESP_OK) {
      free(s_cached_results);
      s_cached_results = NULL;
      return false;
    }
  }

  s_cached_count = count;
  s_is_scan_ready = true;
  return true;
}

bool ap_scanner_start(void) {
  ap_scanner_free_results();
  esp_err_t err = spi_bridge_run_scan(SPI_ID_WIFI_APP_SCAN_AP, SPI_ID_WIFI_SCAN_STATUS, NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "AP scan failed over SPI");
    return false;
  }
  bool ok = fetch_results();
  if (ok) {
    // Auto-persist to SD when a card is present; a no-op otherwise (SD-only).
    ap_scanner_save_results_to_sd_card();
  }
  return ok;
}

wifi_ap_record_t *ap_scanner_get_results(uint16_t *out_count) {
  if (!s_is_scan_ready) {
    if (out_count != NULL)
      *out_count = 0;
    return NULL;
  }
  if (out_count != NULL)
    *out_count = s_cached_count;
  return s_cached_results != NULL ? s_cached_results : &s_empty_record;
}

void ap_scanner_free_results(void) {
  if (s_cached_results != NULL) {
    free(s_cached_results);
    s_cached_results = NULL;
  }
  s_cached_count = 0;
  s_is_scan_ready = false;
}

bool ap_scanner_save_results_to_internal_flash(void) {
  // Policy: files are saved to the micro-SD only, no internal-flash fallback.
  ESP_LOGD(TAG, "Internal-flash save disabled (SD-only policy)");
  return false;
}

bool ap_scanner_save_results_to_sd_card(void) {
  if (s_cached_results == NULL || s_cached_count == 0) {
    return false;
  }

  cJSON *root = cJSON_CreateArray();
  if (root == NULL) {
    return false;
  }

  for (uint16_t i = 0; i < s_cached_count; i++) {
    const wifi_ap_record_t *ap = &s_cached_results[i];
    cJSON *obj = cJSON_CreateObject();
    if (obj == NULL) {
      continue;
    }
    char bssid[18];
    snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X", ap->bssid[0], ap->bssid[1],
             ap->bssid[2], ap->bssid[3], ap->bssid[4], ap->bssid[5]);
    cJSON_AddStringToObject(obj, "ssid", (const char *)ap->ssid);
    cJSON_AddStringToObject(obj, "bssid", bssid);
    cJSON_AddNumberToObject(obj, "rssi", ap->rssi);
    cJSON_AddNumberToObject(obj, "channel", ap->primary);
    cJSON_AddNumberToObject(obj, "auth", ap->authmode);
    cJSON_AddItemToArray(root, obj);
  }

  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    return false;
  }

  // storage_write_string is gated on a mounted SD; without a card it returns an
  // error and nothing is written (results still shown on screen).
  esp_err_t err = storage_write_string(AP_SCAN_SAVE_PATH, json);
  free(json);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved %u APs to %s", s_cached_count, AP_SCAN_SAVE_PATH);
  }
  return err == ESP_OK;
}
