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

#include "client_scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "cJSON.h"
#include "led_control.h"
#include "spi_bridge.h"
#include "storage_write.h"
#include "tos_storage_paths.h"

static const char *TAG = "CLIENT_SCANNER";

// SD-only persistence (no flash fallback); latest scan overwrites.
#define CLIENT_SCAN_SAVE_PATH TOS_PATH_WIFI "/scanned_clients.json"

static client_scanner_record_t s_cached_client;
static client_scanner_record_t *s_cached_results = NULL;
static uint16_t s_cached_count = 0;
static bool s_is_scan_ready = false;
static client_scanner_record_t s_empty_record;

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

  s_cached_results = (client_scanner_record_t *)malloc(count * sizeof(client_scanner_record_t));
  if (s_cached_results == NULL) {
    ESP_LOGW(TAG, "Failed to allocate client results buffer");
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

bool client_scanner_start(void) {
  client_scanner_free_results();
  esp_err_t err = spi_bridge_run_scan(SPI_ID_WIFI_APP_SCAN_CLIENT, SPI_ID_WIFI_SCAN_STATUS, NULL, 0);
  if (err != ESP_OK) {
    led_signal_error();
    return false;
  }
  bool ok = fetch_results();
  if (ok) {
    client_scanner_save_results_to_sd_card();  // SD-only; no-op without a card
    s_cached_count > 0 ? led_signal_info() : led_signal_warning();
  } else {
    led_signal_error();
  }
  return ok;
}

client_scanner_record_t *client_scanner_get_results(uint16_t *out_count) {
  if (!s_is_scan_ready) {
    if (out_count != NULL)
      *out_count = 0;
    return NULL;
  }
  if (out_count != NULL)
    *out_count = s_cached_count;
  return s_cached_results != NULL ? s_cached_results : &s_empty_record;
}

client_scanner_record_t *client_scanner_get_result_by_index(uint16_t index) {
  spi_header_t resp;
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_DATA, (uint8_t *)&index, 2, &resp, (uint8_t *)&s_cached_client, 1000) ==
      ESP_OK) {
    return &s_cached_client;
  }
  return NULL;
}

void client_scanner_free_results(void) {
  if (s_cached_results != NULL) {
    free(s_cached_results);
    s_cached_results = NULL;
  }
  s_cached_count = 0;
  s_is_scan_ready = false;
}

bool client_scanner_save_results_to_internal_flash(void) {
  // Policy: files are saved to the micro-SD only, no internal-flash fallback.
  ESP_LOGD(TAG, "Internal-flash save disabled (SD-only policy)");
  return false;
}

bool client_scanner_save_results_to_sd_card(void) {
  if (s_cached_results == NULL || s_cached_count == 0) {
    return false;
  }

  cJSON *root = cJSON_CreateArray();
  if (root == NULL) {
    return false;
  }

  for (uint16_t i = 0; i < s_cached_count; i++) {
    const client_scanner_record_t *c = &s_cached_results[i];
    cJSON *obj = cJSON_CreateObject();
    if (obj == NULL) {
      continue;
    }
    char bssid[18];
    char client_mac[18];
    snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X", c->bssid[0], c->bssid[1],
             c->bssid[2], c->bssid[3], c->bssid[4], c->bssid[5]);
    snprintf(client_mac, sizeof(client_mac), "%02X:%02X:%02X:%02X:%02X:%02X", c->client_mac[0],
             c->client_mac[1], c->client_mac[2], c->client_mac[3], c->client_mac[4],
             c->client_mac[5]);
    cJSON_AddStringToObject(obj, "bssid", bssid);
    cJSON_AddStringToObject(obj, "client", client_mac);
    cJSON_AddNumberToObject(obj, "rssi", c->rssi);
    cJSON_AddNumberToObject(obj, "channel", c->channel);
    cJSON_AddItemToArray(root, obj);
  }

  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    return false;
  }

  esp_err_t err = storage_write_string(CLIENT_SCAN_SAVE_PATH, json);
  free(json);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved %u clients to %s", s_cached_count, CLIENT_SCAN_SAVE_PATH);
  }
  return err == ESP_OK;
}
