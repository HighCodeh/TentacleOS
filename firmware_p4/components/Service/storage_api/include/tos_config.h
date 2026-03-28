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

#ifndef TOS_CONFIG_H
#define TOS_CONFIG_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Flash fallback paths (assets partition)
#define ASSETS_CONFIG_SCREEN  "/assets/config/screen/screen_config.conf"
#define ASSETS_CONFIG_WIFI    "/assets/config/wifi/wifi_ap.conf"
#define ASSETS_CONFIG_BLE     "/assets/config/bluetooth/ble_announce.conf"
#define ASSETS_CONFIG_LORA    "/assets/config/lora/lora.conf"
#define ASSETS_CONFIG_SYSTEM  "/assets/config/system/system.conf"

typedef struct {
  int  brightness;
  int  rotation;
  char theme[32];
  int  auto_lock_seconds;
  bool auto_dim;
} tos_config_screen_t;

typedef struct {
  char ssid[33];
  char password[65];
  char ip_addr[16];
  int  max_conn;
  bool enabled;
} tos_config_wifi_ap_t;

typedef struct {
  bool enabled;
  char ssid[33];
  char password[65];
} tos_config_wifi_client_t;

typedef struct {
  tos_config_wifi_ap_t     ap;
  tos_config_wifi_client_t client;
} tos_config_wifi_t;

typedef struct {
  char name[32];
  bool enabled;
} tos_config_ble_t;

typedef struct {
  uint32_t frequency;
  int      spreading_factor;
  long     bandwidth;
  int      tx_power;
  uint8_t  sync_word;
  bool     enabled;
} tos_config_lora_t;

typedef struct {
  char name[32];
  char locale[8];
  int  timezone_offset;
  int  volume;
  bool vibration;
  char log_level[8];
  bool first_boot_done;
} tos_config_system_t;

extern tos_config_screen_t g_config_screen;
extern tos_config_wifi_t   g_config_wifi;
extern tos_config_ble_t    g_config_ble;
extern tos_config_lora_t   g_config_lora;
extern tos_config_system_t g_config_system;

esp_err_t tos_config_load(const char *sd_path, const char *flash_path, const char *module);
void      tos_config_load_all(void);
esp_err_t tos_config_save(const char *sd_path, const char *module);

#endif // TOS_CONFIG_H
