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


#include "espnow_chat.h"
#include "service_esp_now.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "App_EspNowChat";

static espnow_chat_on_msg_cb_t s_ui_msg_cb = NULL;
static espnow_chat_on_device_list_cb_t s_ui_refresh_cb = NULL;


static void service_on_recv(const uint8_t *mac_addr, const service_esp_now_packet_t *data, int8_t rssi) {
  if (data->type == ESP_NOW_MSG_TYPE_HELLO) {
    ESP_LOGI(TAG, "Discovery: %s is online", data->nick);
    if (s_ui_refresh_cb) {
      s_ui_refresh_cb();
    }
  } else if (data->type == ESP_NOW_MSG_TYPE_MSG) {
    ESP_LOGI(TAG, "Msg from %s: %s", data->nick, data->text);
    if (s_ui_msg_cb) {
      s_ui_msg_cb(data->nick, data->text, false);
    }
  } else if (data->type == ESP_NOW_MSG_TYPE_KEY_SHARE) {
    ESP_LOGI(TAG, "Secure pairing received from %s", data->nick);
    if (s_ui_msg_cb) {
      char notif[64];
      snprintf(notif, sizeof(notif), "Secure Pair with %s!", data->nick);
      s_ui_msg_cb("System", notif, true);
    }
    if (s_ui_refresh_cb) {
      s_ui_refresh_cb();
    }
  }
}

static void service_on_send(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    ESP_LOGW(TAG, "Message send failed");
    if (s_ui_msg_cb) {
      s_ui_msg_cb("System", "Failed to send message", true);
    }
  }
}


esp_err_t espnow_chat_init(void) {
  ESP_LOGI(TAG, "Initializing Chat Application");

  esp_err_t ret = service_esp_now_init();
  if (ret != ESP_OK) return ret;

  service_esp_now_register_recv_cb(service_on_recv);
  service_esp_now_register_send_cb(service_on_send);

  return ESP_OK;
}

void espnow_chat_deinit(void) {
  service_esp_now_deinit();
}

void espnow_chat_set_nick(const char *nick) {
  service_esp_now_set_nick(nick);
}

const char* espnow_chat_get_nick(void) {
  return service_esp_now_get_nick();
}

void espnow_chat_set_online(bool online) {
  service_esp_now_set_online(online);
}

bool espnow_chat_is_online(void) {
  return service_esp_now_is_online();
}

void espnow_chat_set_security_key(const char *key) {
  service_esp_now_set_key(key);
}

esp_err_t espnow_chat_send_message(const uint8_t *target_mac, const char *message) {
  return service_esp_now_send_msg(target_mac, message);
}

esp_err_t espnow_chat_broadcast_discovery(void) {
  return service_esp_now_broadcast_hello();
}

esp_err_t espnow_chat_secure_pair(const uint8_t *target_mac) {
  return service_esp_now_secure_pair(target_mac);
}

void espnow_chat_register_msg_cb(espnow_chat_on_msg_cb_t cb) {
  s_ui_msg_cb = cb;
}

void espnow_chat_register_refresh_cb(espnow_chat_on_device_list_cb_t cb) {
  s_ui_refresh_cb = cb;
}

int espnow_chat_get_peer_list(espnow_chat_peer_t *peers, int max_count) {
  // Adapter function: Converts service struct to app struct if needed
  // Currently they are almost identical, but decoupled for architecture cleanliness.

  // We need to use a temporary buffer because the types are slightly different (C namespacing)
  // or just cast if binary compatible. Let's do it safely.

  // Assuming service_esp_now_peer_info_t is available in service header.
  // We need to include the full definition of service struct or redefine similar one.
  // The service exposes `service_esp_now_peer_info_t`.

  service_esp_now_peer_info_t service_peers[32]; // Max internal limit
  int count = service_esp_now_get_session_peers(service_peers, (max_count > 32) ? 32 : max_count);

  for (int i = 0; i < count; i++) {
    memcpy(peers[i].mac, service_peers[i].mac, 6);
    strncpy(peers[i].nick, service_peers[i].nick, 16);
    peers[i].rssi = service_peers[i].rssi;
    peers[i].is_saved = service_peers[i].is_permanent;
  }

  return count;
}

esp_err_t espnow_chat_save_peer(const uint8_t *mac, const char *name) {
  return service_esp_now_save_peer_to_conf(mac, name);
}
