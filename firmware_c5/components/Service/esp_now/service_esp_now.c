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

#include "service_esp_now.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "cJSON.h"
#include "storage_write.h"
#include "storage_assets.h"

static const char *TAG = "ESP_NOW_SERVICE";

#define CHAT_CONFIG_FILE "config/chat/chat.conf"
#define CHAT_CONFIG_PATH "/assets/" CHAT_CONFIG_FILE
#define ADDRESSES_CONFIG_FILE "config/chat/addresses.conf"
#define ADDRESSES_CONFIG_PATH "/assets/" ADDRESSES_CONFIG_FILE
#define MAX_SESSION_PEERS 32
#define KEY_MAX_LEN 32

typedef struct {
  uint8_t mac[6];
  char nick[ESP_NOW_NICK_MAX_LEN];
  int8_t rssi;
  uint32_t last_seen;
} session_peer_t;

static session_peer_t *s_session_peers = NULL;
static int s_session_peer_count = 0;

static char s_current_nick[ESP_NOW_NICK_MAX_LEN] = "Highboy";
static char s_encryption_key[KEY_MAX_LEN] = ""; // Empty means no encryption
static bool s_is_online = true;
static service_esp_now_recv_cb_t s_recv_cb = NULL;
static service_esp_now_send_cb_t s_send_cb = NULL;

// Vigenère Cipher for Printable ASCII (32-126)
static void vigenere_process(char *text, const char *key, bool encrypt) {
  if (text == NULL || key == NULL || strlen(key) == 0) return;

  int text_len = strlen(text);
  int key_len = strlen(key);

  for (int i = 0; i < text_len; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c >= 32 && c <= 126) {
      unsigned char k = (unsigned char)key[i % key_len];
      int k_val = (k >= 32 && k <= 126) ? (k - 32) : 0;
      int c_val = c - 32;

      int new_val;
      if (encrypt) {
        new_val = (c_val + k_val) % 95;
      } else {
        new_val = (c_val - k_val + 95) % 95;
      }

      text[i] = (char)(new_val + 32);
    }
  }
}

static void generate_random_key(char *out_key, size_t len) {
  const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
  if (len > 0) {
    for (size_t i = 0; i < len - 1; i++) {
      int key = esp_random();
      out_key[i] = charset[key % (sizeof(charset) - 1)];
    }
    out_key[len - 1] = '\0';
  }
}

static void update_session_peer(const uint8_t *mac, const char *nick, int8_t rssi) {
  if (s_session_peers == NULL) {
    s_session_peers = heap_caps_malloc(sizeof(session_peer_t) * MAX_SESSION_PEERS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_session_peers == NULL) {
      s_session_peers = malloc(sizeof(session_peer_t) * MAX_SESSION_PEERS);
    }
    memset(s_session_peers, 0, sizeof(session_peer_t) * MAX_SESSION_PEERS);
  }

  int found_idx = -1;
  for (int i = 0; i < s_session_peer_count; i++) {
    if (memcmp(s_session_peers[i].mac, mac, 6) == 0) {
      found_idx = i;
      break;
    }
  }

  if (found_idx != -1) {
    strncpy(s_session_peers[found_idx].nick, nick, ESP_NOW_NICK_MAX_LEN - 1);
    s_session_peers[found_idx].rssi = rssi;
    s_session_peers[found_idx].last_seen = esp_log_timestamp();
  } else if (s_session_peer_count < MAX_SESSION_PEERS) {
    memcpy(s_session_peers[s_session_peer_count].mac, mac, 6);
    strncpy(s_session_peers[s_session_peer_count].nick, nick, ESP_NOW_NICK_MAX_LEN - 1);
    s_session_peers[s_session_peer_count].rssi = rssi;
    s_session_peers[s_session_peer_count].last_seen = esp_log_timestamp();
    s_session_peer_count++;
    ESP_LOGI(TAG, "New session peer discovered: %s (" MACSTR ")", nick, MAC2STR(mac));
  }
}

static bool is_valid_nick(const char *nick) {
  if (nick == NULL || strlen(nick) == 0) return false;
  bool has_printable = false;
  for (int i = 0; nick[i] != '\0'; i++) {
    if (!isprint((unsigned char)nick[i])) return false;
    if (!isspace((unsigned char)nick[i])) has_printable = true;
  }
  return has_printable;
}

static void load_peers_from_conf(void) {
  size_t length = 0;
  char *data = (char *)storage_assets_load_file(ADDRESSES_CONFIG_FILE, &length);
  if (data == NULL) return;

  cJSON *json = cJSON_Parse(data);
  if (json && cJSON_IsArray(json)) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, json) {
      cJSON *j_mac = cJSON_GetObjectItem(item, "mac");
      if (cJSON_IsString(j_mac)) {
        uint8_t mac[6];
        int values[6];
        if (sscanf(j_mac->valuestring, "%x:%x:%x:%x:%x:%x", 
                   &values[0], &values[1], &values[2], 
                   &values[3], &values[4], &values[5]) == 6) 
        {
          for(int i = 0; i < 6; ++i) mac[i] = (uint8_t)values[i];
          service_esp_now_add_peer(mac);
        }
      }
    }
    cJSON_Delete(json);
  }
  free(data);
}

static void load_chat_config(void) {
  size_t length = 0;
  char *data = (char *)storage_assets_load_file(CHAT_CONFIG_FILE, &length);
  if (data == NULL) {
    ESP_LOGW(TAG, "Chat config not found, using defaults");
    return;
  }

  cJSON *json = cJSON_Parse(data);
  if (json) {
    cJSON *nick = cJSON_GetObjectItem(json, "nick");
    if (cJSON_IsString(nick) && (nick->valuestring != NULL)) {
      strncpy(s_current_nick, nick->valuestring, ESP_NOW_NICK_MAX_LEN - 1);
      s_current_nick[ESP_NOW_NICK_MAX_LEN - 1] = '\0';
    }
    cJSON *online = cJSON_GetObjectItem(json, "online");
    if (cJSON_IsBool(online)) {
      s_is_online = cJSON_IsTrue(online);
    }
    cJSON *key = cJSON_GetObjectItem(json, "key");
    if (cJSON_IsString(key) && (key->valuestring != NULL)) {
      strncpy(s_encryption_key, key->valuestring, KEY_MAX_LEN - 1);
      s_encryption_key[KEY_MAX_LEN - 1] = '\0';
    }
    cJSON_Delete(json);
  }
  free(data);
}

static esp_err_t save_chat_config(void) {
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "nick", s_current_nick);
  cJSON_AddBoolToObject(json, "online", s_is_online);
  cJSON_AddStringToObject(json, "key", s_encryption_key);

  char *string = cJSON_Print(json);
  if (string == NULL) {
    cJSON_Delete(json);
    return ESP_FAIL;
  }

  esp_err_t err = storage_write_string(CHAT_CONFIG_PATH, string);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save chat config: %s", esp_err_to_name(err));
  }

  free(string);
  cJSON_Delete(json);
  return err;
}

static void internal_on_data_sent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  if (!s_is_online) return; 

  const uint8_t *mac_addr = tx_info->des_addr;
  ESP_LOGD(TAG, "Last Send Status: %s", status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
  if (s_send_cb) {
    s_send_cb(mac_addr, status);
  }
}

static void internal_on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (!s_is_online) return;

  const uint8_t *mac_addr = recv_info->src_addr;
  int8_t rssi = recv_info->rx_ctrl->rssi;

  if (len != sizeof(service_esp_now_packet_t)) {
    ESP_LOGW(TAG, "Received packet with invalid length: %d (expected %d)", len, sizeof(service_esp_now_packet_t));
    return;
  }

  if (!esp_now_is_peer_exist(mac_addr)) {
    ESP_LOGI(TAG, "Auto-pairing with new peer: " MACSTR, MAC2STR(mac_addr));
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    peer.channel = 0; // Use current channel
    peer.ifidx = WIFI_IF_STA; // Default to Station interface
    peer.encrypt = false;
    memcpy(peer.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);

    esp_err_t add_err = esp_now_add_peer(&peer);
    if (add_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to auto-pair peer: %s", esp_err_to_name(add_err));
    }
  }

  // Copy data to modify it (decrypt)
  service_esp_now_packet_t packet_buf;
  memcpy(&packet_buf, data, sizeof(service_esp_now_packet_t));

  // Handle KEY SHARE
  if (packet_buf.type == ESP_NOW_MSG_TYPE_KEY_SHARE) {
    ESP_LOGI(TAG, "Received KEY SHARE from " MACSTR, MAC2STR(mac_addr));
    // Save the key received
    service_esp_now_set_key(packet_buf.text);

    // Notify user via callback (optional, or just log)
    // We reuse MSG type for UI but with a system message
    /*
        if (s_recv_cb) {
            service_esp_now_packet_t sys_msg;
            sys_msg.type = ESP_NOW_MSG_TYPE_MSG;
            strcpy(sys_msg.nick, "SYSTEM");
            strcpy(sys_msg.text, "Secure Key Received & Saved");
            s_recv_cb(mac_addr, &sys_msg, rssi);
            return;
        }
        */
  }

  // Decrypt content if it's a message and we have a key
  if (packet_buf.type == ESP_NOW_MSG_TYPE_MSG && strlen(s_encryption_key) > 0) {
    vigenere_process(packet_buf.text, s_encryption_key, false);
  }

  if (s_recv_cb) {
    s_recv_cb(mac_addr, &packet_buf, rssi);
  }

  // Update session list with sender info
  update_session_peer(mac_addr, packet_buf.nick, rssi);
}

esp_err_t service_esp_now_secure_pair(const uint8_t *target_mac) {
  if (!s_is_online) return ESP_ERR_INVALID_STATE;
  if (target_mac == NULL) return ESP_ERR_INVALID_ARG;

  // Generate new key if we don't have one
  if (strlen(s_encryption_key) == 0) {
    char new_key[KEY_MAX_LEN];
    generate_random_key(new_key, 16); // Generate 16 char key
    service_esp_now_set_key(new_key);
    ESP_LOGI(TAG, "Generated new secure key: %s", new_key);
  }

  // Ensure peer exists
  service_esp_now_add_peer(target_mac);

  service_esp_now_packet_t packet;
  memset(&packet, 0, sizeof(packet));

  packet.type = ESP_NOW_MSG_TYPE_KEY_SHARE;
  strncpy(packet.nick, s_current_nick, ESP_NOW_NICK_MAX_LEN - 1);
  // In KEY_SHARE packet, the 'text' field contains the key
  strncpy(packet.text, s_encryption_key, ESP_NOW_TEXT_MAX_LEN - 1);

  // Send unencrypted so the other side can read it
  return esp_now_send(target_mac, (uint8_t *)&packet, sizeof(packet));
}

esp_err_t service_esp_now_init(void) {
  load_chat_config();

  esp_err_t ret = esp_now_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error initializing ESP-NOW: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_now_register_recv_cb(internal_on_data_recv);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error registering recv cb: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_now_register_send_cb(internal_on_data_sent);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error registering send cb: %s", esp_err_to_name(ret));
    return ret;
  }

  load_peers_from_conf();

  ESP_LOGI(TAG, "ESP-NOW Initialized. Nick: %s, Online: %d", s_current_nick, s_is_online);
  return ESP_OK;
}

void service_esp_now_deinit(void) {
  esp_now_deinit();
  ESP_LOGI(TAG, "ESP-NOW De-initialized");
}

esp_err_t service_esp_now_set_nick(const char *nick) {
  if (!is_valid_nick(nick)) {
    ESP_LOGE(TAG, "Invalid nick provided");
    return ESP_ERR_INVALID_ARG;
  }
  strncpy(s_current_nick, nick, ESP_NOW_NICK_MAX_LEN - 1);
  s_current_nick[ESP_NOW_NICK_MAX_LEN - 1] = '\0';

  return save_chat_config();
}

const char* service_esp_now_get_nick(void) {
  return s_current_nick;
}

esp_err_t service_esp_now_set_online(bool online) {
  s_is_online = online;
  return save_chat_config();
}

bool service_esp_now_is_online(void) {
  return s_is_online;
}

esp_err_t service_esp_now_broadcast_hello(void) {
  if (!s_is_online) return ESP_ERR_INVALID_STATE;

  service_esp_now_packet_t packet;
  memset(&packet, 0, sizeof(packet));

  packet.type = ESP_NOW_MSG_TYPE_HELLO;
  strncpy(packet.nick, s_current_nick, ESP_NOW_NICK_MAX_LEN - 1);

  uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  if (!esp_now_is_peer_exist(broadcast_mac)) {
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    memcpy(peer.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    esp_now_add_peer(&peer);
  }

  return esp_now_send(broadcast_mac, (uint8_t *)&packet, sizeof(packet));
}

esp_err_t service_esp_now_send_msg(const uint8_t *target_mac, const char *text) {
  if (!s_is_online) return ESP_ERR_INVALID_STATE;
  if (target_mac == NULL) return ESP_ERR_INVALID_ARG;

  if (!esp_now_is_peer_exist(target_mac)) {
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    memcpy(peer.peer_addr, target_mac, ESP_NOW_ETH_ALEN);
    esp_err_t add_err = esp_now_add_peer(&peer);
    if (add_err != ESP_OK) {
      ESP_LOGE(TAG, "Cannot add peer for sending: %s", esp_err_to_name(add_err));
      return add_err;
    }
  }

  service_esp_now_packet_t packet;
  memset(&packet, 0, sizeof(packet));

  packet.type = ESP_NOW_MSG_TYPE_MSG;
  strncpy(packet.nick, s_current_nick, ESP_NOW_NICK_MAX_LEN - 1);
  strncpy(packet.text, text, ESP_NOW_TEXT_MAX_LEN - 1);

  // Encrypt if key is set
  if (strlen(s_encryption_key) > 0) {
    vigenere_process(packet.text, s_encryption_key, true);
  }

  return esp_now_send(target_mac, (uint8_t *)&packet, sizeof(packet));
}

void service_esp_now_register_recv_cb(service_esp_now_recv_cb_t cb) {
  s_recv_cb = cb;
}

void service_esp_now_register_send_cb(service_esp_now_send_cb_t cb) {
  s_send_cb = cb;
}

bool service_esp_now_is_peer_exist(const uint8_t *mac_addr) {
  return esp_now_is_peer_exist(mac_addr);
}

esp_err_t service_esp_now_add_peer(const uint8_t *mac_addr) {
  if (esp_now_is_peer_exist(mac_addr)) {
    return ESP_OK;
  }

  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  peer.channel = 0;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  memcpy(peer.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);

  return esp_now_add_peer(&peer);
}

esp_err_t service_esp_now_set_key(const char *key) {
  if (key == NULL) return ESP_ERR_INVALID_ARG;
  // Check for printable characters only
  for (int i = 0; key[i] != '\0'; i++) {
    if (key[i] < 32 || key[i] > 126) return ESP_ERR_INVALID_ARG;
  }

  strncpy(s_encryption_key, key, KEY_MAX_LEN - 1);
  s_encryption_key[KEY_MAX_LEN - 1] = '\0';

  return save_chat_config();
}

const char* service_esp_now_get_key(void) {
  return s_encryption_key;
}

int service_esp_now_get_session_peers(service_esp_now_peer_info_t *out_peers, int max_peers) {
  if (s_session_peers == NULL || out_peers == NULL) return 0;

  int count = (s_session_peer_count < max_peers) ? s_session_peer_count : max_peers;

  size_t length = 0;
  char *data = (char *)storage_assets_load_file(ADDRESSES_CONFIG_FILE, &length);
  cJSON *root = data ? cJSON_Parse(data) : NULL;

  for (int i = 0; i < count; i++) {
    memcpy(out_peers[i].mac, s_session_peers[i].mac, 6);
    strncpy(out_peers[i].nick, s_session_peers[i].nick, ESP_NOW_NICK_MAX_LEN - 1);
    out_peers[i].rssi = s_session_peers[i].rssi;
    out_peers[i].is_permanent = false;

    if (root && cJSON_IsArray(root)) {
      char mac_str[18];
      snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
               s_session_peers[i].mac[0], s_session_peers[i].mac[1], s_session_peers[i].mac[2],
               s_session_peers[i].mac[3], s_session_peers[i].mac[4], s_session_peers[i].mac[5]);

      cJSON *item = NULL;
      cJSON_ArrayForEach(item, root) {
        cJSON *j_mac = cJSON_GetObjectItem(item, "mac");
        if (j_mac && cJSON_IsString(j_mac) && strcmp(j_mac->valuestring, mac_str) == 0) {
          out_peers[i].is_permanent = true;
          cJSON *j_name = cJSON_GetObjectItem(item, "name");
          if (j_name && cJSON_IsString(j_name)) {
            strncpy(out_peers[i].nick, j_name->valuestring, ESP_NOW_NICK_MAX_LEN - 1);
          }
          break;
        }
      }
    }
  }

  if (root) cJSON_Delete(root);
  if (data) free(data);
  return count;
}

esp_err_t service_esp_now_save_peer_to_conf(const uint8_t *mac_addr, const char *name) {
  if (!is_valid_nick(name)) return ESP_ERR_INVALID_ARG;

  size_t length = 0;
  char *data = (char *)storage_assets_load_file(ADDRESSES_CONFIG_FILE, &length);
  cJSON *root = data ? cJSON_Parse(data) : NULL;

  if (!root) {
    root = cJSON_CreateArray();
  }

  // Check for duplicate MAC and update name if found
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  bool found = false;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, root) {
    cJSON *j_mac = cJSON_GetObjectItem(item, "mac");
    if (j_mac && cJSON_IsString(j_mac) && strcmp(j_mac->valuestring, mac_str) == 0) {
      cJSON_ReplaceItemInObject(item, "name", cJSON_CreateString(name));
      found = true;
      break;
    }
  }

  if (!found) {
    cJSON *new_peer = cJSON_CreateObject();
    cJSON_AddStringToObject(new_peer, "mac", mac_str);
    cJSON_AddStringToObject(new_peer, "name", name);
    cJSON_AddItemToArray(root, new_peer);
  }

  char *out = cJSON_Print(root);
  esp_err_t err = storage_write_string(ADDRESSES_CONFIG_PATH, out);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save peers to config: %s", esp_err_to_name(err));
  }

  free(out);
  if (data) free(data);
  cJSON_Delete(root);
  return err;
}
