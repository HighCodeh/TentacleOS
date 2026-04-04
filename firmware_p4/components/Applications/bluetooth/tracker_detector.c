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

#include "tracker_detector.h"
#include "spi_bridge.h"
#include "esp_log.h"
#include <stdlib.h>

static tracker_record_t *cached_results = NULL;
static uint16_t cached_count = 0;
static uint16_t cached_capacity = 0;
static tracker_record_t empty_record;

bool tracker_detector_start(void) {
    tracker_detector_clear_results();
    return (spi_bridge_send_command(SPI_ID_BT_APP_TRACKER, NULL, 0, NULL, NULL, 2000) == ESP_OK);
}

void tracker_detector_stop(void) {
    spi_bridge_send_command(SPI_ID_BT_APP_STOP, NULL, 0, NULL, NULL, 2000);
    tracker_detector_clear_results();
}

tracker_record_t* tracker_detector_get_results(uint16_t *count) {
    spi_header_t resp;
    uint8_t payload[2];
    uint16_t magic_count = SPI_DATA_INDEX_COUNT;

    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_count, 2, &resp, payload, 1000) != ESP_OK) {
        if (count) *count = cached_count;
        return cached_results ? cached_results : &empty_record;
    }

    uint16_t remote_count = 0;
    memcpy(&remote_count, payload, 2);
    if (remote_count > MAX_TRACKERS_FOUND) remote_count = MAX_TRACKERS_FOUND;

    if (remote_count < cached_count) cached_count = 0;

    if (remote_count > cached_capacity) {
        tracker_record_t *new_buf = (tracker_record_t *)realloc(cached_results, remote_count * sizeof(tracker_record_t));
        if (!new_buf) {
            if (count) *count = cached_count;
            return cached_results ? cached_results : &empty_record;
        }
        cached_results = new_buf;
        cached_capacity = remote_count;
    }

    uint16_t fetched = cached_count;
    for (uint16_t i = cached_count; i < remote_count; i++) {
        if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&i, 2, &resp, (uint8_t*)&cached_results[i], 1000) != ESP_OK) {
            break;
        }
        fetched = i + 1;
    }

    cached_count = fetched;
    if (count) *count = cached_count;
    return cached_results ? cached_results : &empty_record;
}

void tracker_detector_clear_results(void) {
    if (cached_results) {
        free(cached_results);
        cached_results = NULL;
    }
    cached_count = 0;
    cached_capacity = 0;
}
