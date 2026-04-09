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

#include "wifi_flood.h"

#include <string.h>

#include "esp_log.h"

#include "spi_bridge.h"

static const char *TAG = "WIFI_FLOOD";

#define WIFI_FLOOD_TYPE_AUTH  0
#define WIFI_FLOOD_TYPE_ASSOC 1
#define WIFI_FLOOD_TYPE_PROBE 2

static bool s_is_running = false;

static esp_err_t send_flood(uint8_t type, const uint8_t *target_bssid, uint8_t channel) {
  uint8_t payload[8];
  payload[0] = type;
  if (target_bssid != NULL)
    memcpy(payload + 1, target_bssid, 6);
  else
    memset(payload + 1, 0xFF, 6);
  payload[7] = channel;
  return spi_bridge_send_command(SPI_ID_WIFI_APP_FLOOD, payload, sizeof(payload), NULL, NULL, 2000);
}

bool wifi_flood_auth_start(const uint8_t *target_bssid, uint8_t channel) {
  esp_err_t err = send_flood(WIFI_FLOOD_TYPE_AUTH, target_bssid, channel);
  s_is_running = (err == ESP_OK);
  if (!s_is_running)
    ESP_LOGW(TAG, "Wi-Fi auth flood failed over SPI");
  return s_is_running;
}

bool wifi_flood_assoc_start(const uint8_t *target_bssid, uint8_t channel) {
  esp_err_t err = send_flood(WIFI_FLOOD_TYPE_ASSOC, target_bssid, channel);
  s_is_running = (err == ESP_OK);
  if (!s_is_running)
    ESP_LOGW(TAG, "Wi-Fi assoc flood failed over SPI");
  return s_is_running;
}

bool wifi_flood_probe_start(const uint8_t *target_bssid, uint8_t channel) {
  esp_err_t err = send_flood(WIFI_FLOOD_TYPE_PROBE, target_bssid, channel);
  s_is_running = (err == ESP_OK);
  if (!s_is_running)
    ESP_LOGW(TAG, "Wi-Fi probe flood failed over SPI");
  return s_is_running;
}

void wifi_flood_stop(void) {
  spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
  s_is_running = false;
}

bool wifi_flood_is_running(void) {
  return s_is_running;
}
