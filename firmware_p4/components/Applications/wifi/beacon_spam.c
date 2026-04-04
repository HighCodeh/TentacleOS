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

#include "beacon_spam.h"
#include "spi_bridge.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "BEACON_SPAM";
static bool s_running = false;

bool beacon_spam_start_custom(const char *json_path) {
    if (!json_path) return false;
    size_t len = strnlen(json_path, SPI_MAX_PAYLOAD - 1);
    esp_err_t err = spi_bridge_send_command(SPI_ID_WIFI_APP_BEACON_SPAM, (uint8_t*)json_path, (uint8_t)len, NULL, NULL, 2000);
    s_running = (err == ESP_OK);
    if (!s_running) ESP_LOGW(TAG, "Failed to start beacon spam (custom list) over SPI.");
    return s_running;
}

bool beacon_spam_start_random(void) {
    esp_err_t err = spi_bridge_send_command(SPI_ID_WIFI_APP_BEACON_SPAM, NULL, 0, NULL, NULL, 2000);
    s_running = (err == ESP_OK);
    if (!s_running) ESP_LOGW(TAG, "Failed to start beacon spam (random) over SPI.");
    return s_running;
}

void beacon_spam_stop(void) {
    spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
    s_running = false;
}

bool beacon_spam_is_running(void) {
    return s_running;
}
