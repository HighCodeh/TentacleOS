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


#ifndef ESPNOW_CHAT_H
#define ESPNOW_CHAT_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*espnow_chat_on_msg_cb_t)(const char *sender_nick, const char *message, bool is_system_msg);
typedef void (*espnow_chat_on_device_list_cb_t)(void); // Request UI to refresh device list

esp_err_t espnow_chat_init(void);
void espnow_chat_deinit(void);

void espnow_chat_set_nick(const char *nick);
const char* espnow_chat_get_nick(void);
void espnow_chat_set_online(bool online);
bool espnow_chat_is_online(void);
void espnow_chat_set_security_key(const char *key);

esp_err_t espnow_chat_send_message(const uint8_t *target_mac, const char *message);
esp_err_t espnow_chat_broadcast_discovery(void); 
esp_err_t espnow_chat_secure_pair(const uint8_t *target_mac);

void espnow_chat_register_msg_cb(espnow_chat_on_msg_cb_t cb);
void espnow_chat_register_refresh_cb(espnow_chat_on_device_list_cb_t cb);

typedef struct {
    uint8_t mac[6];
    char nick[16]; // Copy from service def
    int8_t rssi;
    bool is_saved;
} espnow_chat_peer_t;

int espnow_chat_get_peer_list(espnow_chat_peer_t *peers, int max_count);
esp_err_t espnow_chat_save_peer(const uint8_t *mac, const char *name);

#endif // ESPNOW_CHAT_H
