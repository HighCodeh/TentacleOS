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


#include "deauther_detector.h"
#include "spi_bridge.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DEAUTH_DETECTOR";
void deauther_detector_start(void) {
    if (spi_bridge_send_command(SPI_ID_WIFI_APP_DEAUTH_DET, NULL, 0, NULL, NULL, 2000) == ESP_OK) {
    } else {
        ESP_LOGW(TAG, "Failed to start deauth detector over SPI.");
    }
}

void deauther_detector_stop(void) {
    spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
}

uint32_t deauther_detector_get_count(void) {
    spi_header_t resp;
    uint8_t payload[4];
    uint16_t index = SPI_DATA_INDEX_DEAUTH_COUNT;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&index, 2, &resp, payload, 1000) == ESP_OK) {
        uint32_t count = 0;
        memcpy(&count, payload, sizeof(count));
        return count;
    }
    return 0;
}
