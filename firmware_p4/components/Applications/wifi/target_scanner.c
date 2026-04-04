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


#include "target_scanner.h"
#include "spi_bridge.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "TARGET_SCANNER";

#define TARGET_SCAN_MAX_RESULTS 200

static target_client_record_t cached_results[TARGET_SCAN_MAX_RESULTS];
static uint16_t cached_count = 0;

static bool fetch_results(uint16_t count) {
    if (count == 0) {
        cached_count = 0;
        return true;
    }
    if (count > TARGET_SCAN_MAX_RESULTS) {
        count = TARGET_SCAN_MAX_RESULTS;
    }
    spi_header_t resp;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = i;
        if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&idx, 2, &resp,
                                    (uint8_t*)&cached_results[i],
                                    sizeof(target_client_record_t)) != ESP_OK) {
            return false;
        }
    }
    cached_count = count;
    return true;
}

bool target_scanner_start(const uint8_t *target_bssid, uint8_t channel) {
    if (!target_bssid) return false;
    uint8_t payload[7];
    memcpy(payload, target_bssid, 6);
    payload[6] = channel;
    esp_err_t err = spi_bridge_send_command(SPI_ID_WIFI_TARGET_SCAN_START, payload, sizeof(payload), NULL, NULL, 2000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Target scan start failed over SPI.");
        return false;
    }
    cached_count = 0;
    return true;
}

target_client_record_t* target_scanner_get_results(uint16_t *count) {
    bool scanning = false;
    uint16_t live_count = 0;
    target_client_record_t *results = target_scanner_get_live_results(&live_count, &scanning);
    if (scanning) return NULL;
    if (count) *count = live_count;
    return results;
}

target_client_record_t* target_scanner_get_live_results(uint16_t *count, bool *scanning) {
    spi_header_t resp;
    uint8_t payload[3] = {0};
    if (spi_bridge_send_command(SPI_ID_WIFI_TARGET_SCAN_STATUS, NULL, 0, &resp, payload, sizeof(payload)) != ESP_OK) {
        if (count) *count = 0;
        if (scanning) *scanning = false;
        return NULL;
    }
    bool is_scanning = (payload[0] != 0);
    uint16_t total = 0;
    memcpy(&total, payload + 1, sizeof(uint16_t));
    if (scanning) *scanning = is_scanning;

    if (!fetch_results(total)) {
        if (count) *count = 0;
        return NULL;
    }

    if (count) *count = cached_count;
    return (cached_count > 0) ? cached_results : NULL;
}

void target_scanner_free_results(void) {
    spi_bridge_send_command(SPI_ID_WIFI_TARGET_FREE, NULL, 0, NULL, NULL, 2000);
    cached_count = 0;
    memset(cached_results, 0, sizeof(cached_results));
}

bool target_scanner_save_results_to_internal_flash(void) {
    return (spi_bridge_send_command(SPI_ID_WIFI_TARGET_SAVE_FLASH, NULL, 0, NULL, NULL, 5000) == ESP_OK);
}

bool target_scanner_save_results_to_sd_card(void) {
    return (spi_bridge_send_command(SPI_ID_WIFI_TARGET_SAVE_SD, NULL, 0, NULL, NULL, 5000) == ESP_OK);
}
