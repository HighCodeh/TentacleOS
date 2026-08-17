// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file tos_config.h
 * @brief System configuration management (load/save JSON configs).
 */

#ifndef TOS_CONFIG_H
#define TOS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "tos_flash_paths.h"

// Flash fallback aliases for tos_config_load_all()
#define ASSETS_CONFIG_SCREEN FLASH_CONFIG_SCREEN
#define ASSETS_CONFIG_WIFI   FLASH_CONFIG_WIFI_AP
#define ASSETS_CONFIG_BLE    FLASH_CONFIG_BLE
#define ASSETS_CONFIG_LORA   FLASH_CONFIG_LORA
#define ASSETS_CONFIG_SYSTEM FLASH_CONFIG_SYSTEM
#define ASSETS_CONFIG_LED    FLASH_CONFIG_LED

typedef struct {
  int brightness;
  int rotation;
  char theme[32];
  int auto_lock_seconds;
  bool auto_dim;
} tos_config_screen_t;

typedef struct {
  char ssid[33];
  char password[65];
  char ip_addr[16];
  int max_conn;
  bool enabled;
} tos_config_wifi_ap_t;

typedef struct {
  bool enabled;
  char ssid[33];
  char password[65];
} tos_config_wifi_client_t;

typedef struct {
  bool enabled;
  tos_config_wifi_ap_t ap;
  tos_config_wifi_client_t client;
} tos_config_wifi_t;

typedef struct {
  char name[32];
  bool enabled;
} tos_config_ble_t;

typedef struct {
  uint32_t frequency;
  int spreading_factor;
  long bandwidth;
  int tx_power;
  uint8_t sync_word;
  bool enabled;
} tos_config_lora_t;

typedef struct {
  char name[32];
  char locale[8];
  int timezone_offset;
  int volume;
  bool vibration;
  char log_level[8];
  bool first_boot_done;
} tos_config_system_t;

// RGB status LED (LP5816). brightness is 0-100 %. Colors are 0xRRGGBB and map to
// the three semantic signals; stored in the file as "#RRGGBB" hex strings.
typedef struct {
  int brightness;
  uint32_t info_color;    // informative (default purple)
  uint32_t warning_color; // warning (default yellow)
  uint32_t error_color;   // error (default red)
} tos_config_led_t;

extern tos_config_screen_t g_config_screen;
extern tos_config_wifi_t g_config_wifi;
extern tos_config_ble_t g_config_ble;
extern tos_config_lora_t g_config_lora;
extern tos_config_system_t g_config_system;
extern tos_config_led_t g_config_led;

esp_err_t tos_config_load(const char *sd_path, const char *flash_path, const char *module);
void tos_config_load_all(void);
esp_err_t tos_config_save(const char *sd_path, const char *module);

#ifdef __cplusplus
}
#endif

#endif // TOS_CONFIG_H
