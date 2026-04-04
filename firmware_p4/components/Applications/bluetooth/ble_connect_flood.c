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

#include "ble_connect_flood.h"
#include "spi_bridge.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "BLE_FLOOD";
static bool is_running = false;

esp_err_t ble_connect_flood_start(const uint8_t *addr, uint8_t addr_type) {
    if (!addr) return ESP_ERR_INVALID_ARG;
    uint8_t payload[7];
    memcpy(payload, addr, 6);
    payload[6] = addr_type;
    esp_err_t err = spi_bridge_send_command(SPI_ID_BT_APP_FLOOD, payload, sizeof(payload), NULL, NULL, 2000);
    is_running = (err == ESP_OK);
    if (!is_running) ESP_LOGW(TAG, "BLE flood failed over SPI.");
    return err;
}

esp_err_t ble_connect_flood_stop(void) {
    spi_bridge_send_command(SPI_ID_BT_APP_STOP, NULL, 0, NULL, NULL, 2000);
    is_running = false;
    return ESP_OK;
}

bool ble_connect_flood_is_running(void) {
    return is_running;
}
