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

#ifndef BLUETOOTH_SERVICE_H
#define BLUETOOTH_SERVICE_H

#include "esp_err.h"
#include "stdint.h"
#include <stdbool.h>
struct ble_gap_event;

#define BLE_SCAN_LIST_SIZE 50

typedef struct {
  char name[32]; 
  char uuids[128];
  uint8_t addr[6];
  uint8_t addr_type;
  int rssi;
} ble_scan_result_t;

typedef void (*ble_sniffer_cb_t)(const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len);

typedef void (*ble_tracker_cb_t)(int rssi);

esp_err_t bluetooth_service_init(void);
esp_err_t bluetooth_service_deinit(void);
esp_err_t bluetooth_service_start(void);
esp_err_t bluetooth_service_stop(void);
bool bluetooth_service_is_initialized(void);
bool bluetooth_service_is_running(void);
void bluetooth_service_disconnect_all(void);
int bluetooth_service_get_connected_count(void);
void bluetooth_service_get_mac(uint8_t *mac);
esp_err_t bluetooth_service_set_random_mac(void);
esp_err_t bluetooth_service_connect(const uint8_t *addr, uint8_t addr_type, int (*cb)(struct ble_gap_event *event, void *arg));

esp_err_t bluetooth_service_start_sniffer(ble_sniffer_cb_t cb);
void bluetooth_service_stop_sniffer(void);

esp_err_t bluetooth_service_start_tracker(const uint8_t *addr, ble_tracker_cb_t cb);
void bluetooth_service_stop_tracker(void);

esp_err_t bluetooth_service_start_advertising(void);
esp_err_t bluetooth_service_stop_advertising(void);
uint8_t bluetooth_service_get_own_addr_type(void);
esp_err_t bluetooth_service_set_max_power(void);
esp_err_t bluetooth_save_announce_config(const char *name, uint8_t max_conn);
esp_err_t bluetooth_load_spam_list(char ***list, size_t *count);
esp_err_t bluetooth_save_spam_list(const char * const *list, size_t count);
void bluetooth_free_spam_list(char **list, size_t count);
void bluetooth_service_scan(uint32_t duration_ms);
uint16_t bluetooth_service_get_scan_count(void);
ble_scan_result_t* bluetooth_service_get_scan_result(uint16_t index);

#endif // BLUETOOTH_SERVICE_H
