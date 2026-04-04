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


#include "ap_scanner.h"
#include "spi_bridge.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "AP_SCANNER";
static wifi_ap_record_t *cached_results = NULL;
static uint16_t cached_count = 0;
static bool scan_ready = false;
static wifi_ap_record_t empty_record;

static bool ap_scanner_fetch_results(void) {
    spi_header_t resp;
    uint8_t payload[2];
    uint16_t magic_count = SPI_DATA_INDEX_COUNT;

    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_count, 2, &resp, payload, 1000) != ESP_OK) {
        return false;
    }

    uint16_t count = 0;
    memcpy(&count, payload, 2);

    if (cached_results) {
        free(cached_results);
        cached_results = NULL;
    }
    cached_count = 0;

    if (count == 0) {
        scan_ready = true;
        return true;
    }

    cached_results = (wifi_ap_record_t *)malloc(count * sizeof(wifi_ap_record_t));
    if (!cached_results) {
        ESP_LOGW(TAG, "Failed to allocate AP results buffer.");
        return false;
    }

    for (uint16_t i = 0; i < count; i++) {
        if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&i, 2, &resp, (uint8_t*)&cached_results[i], 1000) != ESP_OK) {
            free(cached_results);
            cached_results = NULL;
            return false;
        }
    }

    cached_count = count;
    scan_ready = true;
    return true;
}

bool ap_scanner_start(void) {
    ap_scanner_free_results();
    esp_err_t err = spi_bridge_send_command(SPI_ID_WIFI_APP_SCAN_AP, NULL, 0, NULL, NULL, 15000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AP scan failed over SPI.");
        return false;
    }
    return ap_scanner_fetch_results();
}

wifi_ap_record_t* ap_scanner_get_results(uint16_t *count) {
    if (!scan_ready) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = cached_count;
    return cached_results ? cached_results : &empty_record;
}

void ap_scanner_free_results(void) {
    if (cached_results) {
        free(cached_results);
        cached_results = NULL;
    }
    cached_count = 0;
    scan_ready = false;
}

bool ap_scanner_save_results_to_internal_flash(void) {
    return (spi_bridge_send_command(SPI_ID_WIFI_AP_SAVE_FLASH, NULL, 0, NULL, NULL, 5000) == ESP_OK);
}

bool ap_scanner_save_results_to_sd_card(void) {
    return (spi_bridge_send_command(SPI_ID_WIFI_AP_SAVE_SD, NULL, 0, NULL, NULL, 5000) == ESP_OK);
}
