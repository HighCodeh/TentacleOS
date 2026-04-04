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


#ifndef SERVICE_ESP_NOW_H
#define SERVICE_ESP_NOW_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_now.h"
#include "esp_err.h"

// Constants
#define ESP_NOW_NICK_MAX_LEN 16 
#define ESP_NOW_TEXT_MAX_LEN 201 
#define ESP_NOW_BROADCAST_ADDR {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

// Message Types
typedef enum {
    ESP_NOW_MSG_TYPE_HELLO     = 0x01,
    ESP_NOW_MSG_TYPE_MSG       = 0x02,
    ESP_NOW_MSG_TYPE_KEY_SHARE = 0x03
} service_esp_now_msg_type_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    char nick[ESP_NOW_NICK_MAX_LEN];
    char text[ESP_NOW_TEXT_MAX_LEN];
} service_esp_now_packet_t;

// Callback type for received messages
typedef void (*service_esp_now_recv_cb_t)(const uint8_t *mac_addr, const service_esp_now_packet_t *data, int8_t rssi);

// Callback type for send status
typedef void (*service_esp_now_send_cb_t)(const uint8_t *mac_addr, esp_now_send_status_t status);

typedef struct {
    uint8_t mac[6];
    char nick[ESP_NOW_NICK_MAX_LEN];
    int8_t rssi;
    bool is_permanent;
} service_esp_now_peer_info_t;

// API
esp_err_t service_esp_now_init(void);
void service_esp_now_deinit(void);
esp_err_t service_esp_now_set_nick(const char *nick);
const char* service_esp_now_get_nick(void);
esp_err_t service_esp_now_set_online(bool online);
bool service_esp_now_is_online(void);
esp_err_t service_esp_now_broadcast_hello(void);
esp_err_t service_esp_now_send_msg(const uint8_t *target_mac, const char *text);
esp_err_t service_esp_now_secure_pair(const uint8_t *target_mac); // New function
void service_esp_now_register_recv_cb(service_esp_now_recv_cb_t cb);
void service_esp_now_register_send_cb(service_esp_now_send_cb_t cb);
bool service_esp_now_is_peer_exist(const uint8_t *mac_addr);
esp_err_t service_esp_now_add_peer(const uint8_t *mac_addr);
esp_err_t service_esp_now_save_peer_to_conf(const uint8_t *mac_addr, const char *name);
int service_esp_now_get_session_peers(service_esp_now_peer_info_t *out_peers, int max_peers);

esp_err_t service_esp_now_set_key(const char *key);
const char* service_esp_now_get_key(void);
esp_err_t service_esp_now_add_peer(const uint8_t *mac_addr);

#endif // SERVICE_ESP_NOW_H
