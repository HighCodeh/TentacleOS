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


#include "ble_sniffer.h"
#include "bluetooth_service.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "BLE_SNIFFER";

static void sniffer_packet_handler(const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len) {
  // Format: [MAC] RSSI | HEX HEX ...
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

  printf("[%s] %d dBm | ", mac_str, rssi);
  for (int i = 0; i < len; i++) {
    printf("%02X ", data[i]);
  }
  printf("\n");
}

esp_err_t ble_sniffer_start(void) {
  ESP_LOGI(TAG, "Starting BLE Packet Sniffer...");
  return bluetooth_service_start_sniffer(sniffer_packet_handler);
}

void ble_sniffer_stop(void) {
  bluetooth_service_stop_sniffer();
  ESP_LOGI(TAG, "BLE Packet Sniffer Stopped.");
}

