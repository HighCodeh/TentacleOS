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

#include "tos_first_boot.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "storage_init.h"
#include "storage_mkdir.h"
#include "esp_log.h"

#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "first_boot";

static const char *const FIRST_BOOT_DIRS[] = {
  // Protocol roots
  TOS_PATH_NFC,
  TOS_PATH_RFID,
  TOS_PATH_SUBGHZ,
  TOS_PATH_IR,
  TOS_PATH_WIFI,
  TOS_PATH_BLE,
  TOS_PATH_LORA,
  TOS_PATH_BADUSB,

  // Protocol assets
  TOS_PATH_NFC_ASSETS,
  TOS_PATH_RFID_ASSETS,
  TOS_PATH_SUBGHZ_ASSETS,
  TOS_PATH_IR_ASSETS,
  TOS_PATH_WIFI_ASSETS,
  TOS_PATH_BLE_ASSETS,
  TOS_PATH_LORA_ASSETS,
  TOS_PATH_BADUSB_ASSETS,

  // WiFi loot
  TOS_PATH_WIFI_LOOT,
  TOS_PATH_WIFI_LOOT_HS,
  TOS_PATH_WIFI_LOOT_PCAPS,
  TOS_PATH_WIFI_LOOT_DEAUTH,

  // BLE & LoRa loot
  TOS_PATH_BLE_LOOT,
  TOS_PATH_LORA_LOOT,

  // LoRa messages
  TOS_PATH_LORA_MSGS,

  // Captive portal
  TOS_PATH_CAPTIVE,
  TOS_PATH_CAPTIVE_TMPL,

  // Config
  TOS_PATH_CONFIG_DIR,

  // System
  TOS_PATH_THEMES,
  TOS_PATH_RINGTONES,
  TOS_PATH_APPS,
  TOS_PATH_APPS_DATA,
  TOS_PATH_SCRIPTS,
  TOS_PATH_LOGS,
  TOS_PATH_BACKUP,
  TOS_PATH_CACHE,
  TOS_PATH_UPDATE,

  NULL
};

static bool setup_already_done(void)
{
  struct stat st;
  return (stat(TOS_PATH_SETUP_MARKER, &st) == 0);
}

static void mark_setup_done(void)
{
  FILE *f = fopen(TOS_PATH_SETUP_MARKER, "w");
  if (f) {
    fclose(f);
    ESP_LOGI(TAG, "Marker created: %s", TOS_PATH_SETUP_MARKER);
  } else {
    ESP_LOGW(TAG, "Failed to create marker: %s", TOS_PATH_SETUP_MARKER);
  }
}

esp_err_t tos_first_boot_setup(void)
{
  if (!storage_is_mounted()) {
    ESP_LOGW(TAG, "Storage not mounted, skipping first boot setup");
    return ESP_ERR_INVALID_STATE;
  }

  if (setup_already_done()) {
    ESP_LOGI(TAG, "Setup already done, skipping");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "First boot detected — creating folder structure");

  int created = 0;
  int failed = 0;

  for (int i = 0; FIRST_BOOT_DIRS[i] != NULL; i++) {
    esp_err_t ret = storage_mkdir_recursive(FIRST_BOOT_DIRS[i]);
    if (ret == ESP_OK) {
      created++;
    } else {
      ESP_LOGW(TAG, "Failed to create: %s (%s)",
               FIRST_BOOT_DIRS[i], esp_err_to_name(ret));
      failed++;
    }
  }

  ESP_LOGI(TAG, "Folders: %d created, %d failed", created, failed);

  // Create default .conf files on SD if they don't exist
  struct stat st;
  const char *confs[] = { "screen", "wifi", "ble", "lora", "system" };
  const char *paths[] = {
    TOS_PATH_CONFIG_SCREEN, TOS_PATH_CONFIG_WIFI,
    TOS_PATH_CONFIG_BLE, TOS_PATH_CONFIG_LORA, TOS_PATH_CONFIG_SYSTEM
  };

  for (int i = 0; i < 5; i++) {
    if (stat(paths[i], &st) != 0) {
      tos_config_save(paths[i], confs[i]);
      ESP_LOGI(TAG, "Created default: %s", paths[i]);
    }
  }

  if (failed == 0) {
    mark_setup_done();
  }

  return (failed == 0) ? ESP_OK : ESP_FAIL;
}
