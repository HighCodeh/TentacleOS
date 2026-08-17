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

#include "tos_factory_reset.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "storage_assets.h"
#include "storage_init.h"
#include "tos_flash_paths.h"
#include "tos_storage_paths.h"

static const char *TAG = "FACTORY_RESET";

#define FR_PATH_MAX 256

// Config directories on each storage (assets LittleFS + SD). Deleting file
// contents while keeping the directory skeleton avoids re-flashing and keeps
// config saves working right after the reset.
#define FLASH_CONFIG_DIR  FLASH_MOUNT "/config"
#define FLASH_STORAGE_DIR FLASH_MOUNT "/storage"

// SD user-data roots wiped by a full reset. The C5 firmware image
// (VFS_MOUNT_POINT "/c5") is intentionally left out so C5 recovery survives.
static const char *const SD_USER_DIRS[] = {
    TOS_PATH_NFC,
    TOS_PATH_RFID,
    TOS_PATH_SUBGHZ,
    TOS_PATH_IR,
    TOS_PATH_WIFI,
    TOS_PATH_BLE,
    TOS_PATH_LORA,
    TOS_PATH_BADUSB,
    TOS_PATH_THEMES,
    TOS_PATH_RINGTONES,
    TOS_PATH_APPS,
    TOS_PATH_APPS_DATA,
    TOS_PATH_SCRIPTS,
    TOS_PATH_LOGS,
    TOS_PATH_BACKUP,
    TOS_PATH_CACHE,
    TOS_PATH_UPDATE,
};
#define SD_USER_DIRS_COUNT ((int)(sizeof(SD_USER_DIRS) / sizeof(SD_USER_DIRS[0])))

// Recursively delete every regular file under @p dir, leaving the directory
// skeleton in place. Missing directories are treated as success (nothing to do).
static esp_err_t delete_files_recursive(const char *dir) {
  DIR *d = opendir(dir);
  if (d == NULL) {
    return ESP_OK; // absent path: nothing to clear
  }

  esp_err_t result = ESP_OK;
  struct dirent *entry;
  while ((entry = readdir(d)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char path[FR_PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
    if (n <= 0 || n >= (int)sizeof(path)) {
      ESP_LOGW(TAG, "Path too long, skipping: %s/%s", dir, entry->d_name);
      result = ESP_FAIL;
      continue;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      if (delete_files_recursive(path) != ESP_OK) {
        result = ESP_FAIL;
      }
    } else if (unlink(path) != 0) {
      ESP_LOGW(TAG, "Failed to delete %s", path);
      result = ESP_FAIL;
    }
  }

  closedir(d);
  return result;
}

esp_err_t tos_factory_reset_config(void) {
  ESP_LOGW(TAG, "Resetting configuration to factory defaults");

  esp_err_t result = ESP_OK;

  if (storage_assets_is_mounted()) {
    if (delete_files_recursive(FLASH_CONFIG_DIR) != ESP_OK) {
      result = ESP_FAIL;
    }
  }

  if (storage_is_mounted()) {
    if (delete_files_recursive(TOS_PATH_CONFIG_DIR) != ESP_OK) {
      result = ESP_FAIL;
    }
    // Drop the first-boot marker so defaults are re-seeded on the next boot.
    unlink(TOS_PATH_SETUP_MARKER);
  }

  ESP_LOGW(
      TAG, "Configuration reset %s", result == ESP_OK ? "complete" : "completed with warnings");
  return result;
}

esp_err_t tos_factory_reset_all(void) {
  ESP_LOGW(TAG, "Full factory reset: config + user data + NVS");

  esp_err_t result = tos_factory_reset_config();

  // User captures / loot / themes on the assets partition.
  if (storage_assets_is_mounted()) {
    if (delete_files_recursive(FLASH_STORAGE_DIR) != ESP_OK) {
      result = ESP_FAIL;
    }
  }

  // User captures / loot / themes / scripts on the SD card. The C5 firmware
  // image is not in SD_USER_DIRS, so it survives for recovery.
  if (storage_is_mounted()) {
    for (int i = 0; i < SD_USER_DIRS_COUNT; i++) {
      if (delete_files_recursive(SD_USER_DIRS[i]) != ESP_OK) {
        result = ESP_FAIL;
      }
    }
    unlink(TOS_PATH_FAVORITES);
  }

  // NVS: erase and re-init so the system is usable if the caller does not reboot
  // immediately.
  esp_err_t nvs_ret = nvs_flash_erase();
  if (nvs_ret == ESP_OK) {
    nvs_ret = nvs_flash_init();
  }
  if (nvs_ret != ESP_OK) {
    ESP_LOGW(TAG, "NVS erase failed: %s", esp_err_to_name(nvs_ret));
    result = ESP_FAIL;
  }

  ESP_LOGW(TAG, "Full reset %s", result == ESP_OK ? "complete" : "completed with warnings");
  return result;
}
