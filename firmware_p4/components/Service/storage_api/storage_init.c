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

#include "storage_init.h"

#include "esp_log.h"

#include "vfs_config.h"
#include "vfs_core.h"
#include "vfs_sdcard.h"

static const char *TAG = "STORAGE_INIT";

#define HEALTH_CHECK_FILE ".health_check"
#define HEALTH_CHECK_DATA "OK"
#define HEALTH_CHECK_LEN  2
#define HEALTH_READ_BUF   10

// These are implemented by the auto-generated VFS backend glue
extern esp_err_t vfs_init_auto(void);
extern esp_err_t vfs_deinit_auto(void);
extern bool vfs_is_mounted_auto(void);
extern void vfs_print_info(void);

static bool s_is_initialized = false;

esp_err_t storage_init(void) {
  if (s_is_initialized) {
    ESP_LOGW(TAG, "Already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing %s", VFS_BACKEND_NAME);

  esp_err_t ret = vfs_init_auto();
  if (ret == ESP_OK) {
    s_is_initialized = true;
    ESP_LOGI(TAG, "Storage ready at %s", VFS_MOUNT_POINT);
  } else {
    ESP_LOGE(TAG, "Initialization failed: %s", esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_init_custom(uint8_t max_files, bool is_format_on_fail) {
  (void)max_files;
  (void)is_format_on_fail;
  ESP_LOGW(TAG, "Custom initialization not yet implemented, using defaults");
  return storage_init();
}

esp_err_t storage_deinit(void) {
  if (!s_is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Deinitializing storage");

  esp_err_t ret = vfs_deinit_auto();
  if (ret == ESP_OK) {
    s_is_initialized = false;
  }

  return ret;
}

bool storage_is_mounted(void) {
  return s_is_initialized && vfs_is_mounted_auto();
}

esp_err_t storage_remount(void) {
  ESP_LOGI(TAG, "Remounting storage");

  if (s_is_initialized) {
    esp_err_t ret = storage_deinit();
    if (ret != ESP_OK) {
      return ret;
    }
  }

  return storage_init();
}

static esp_err_t check_health(void) {
  if (!storage_is_mounted()) {
    return ESP_ERR_INVALID_STATE;
  }

  char full_path[VFS_MAX_PATH];
  snprintf(full_path, sizeof(full_path), "%s/%s", VFS_MOUNT_POINT, HEALTH_CHECK_FILE);

  esp_err_t ret = vfs_write_file(full_path, HEALTH_CHECK_DATA, HEALTH_CHECK_LEN);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Health check failed: write error");
    return ret;
  }

  char buffer[HEALTH_READ_BUF];
  size_t bytes_read;
  ret = vfs_read_file(full_path, buffer, sizeof(buffer), &bytes_read);
  if (ret != ESP_OK || bytes_read != HEALTH_CHECK_LEN) {
    ESP_LOGE(TAG, "Health check failed: read error");
    vfs_unlink(full_path);
    return ESP_FAIL;
  }

  vfs_unlink(full_path);
  ESP_LOGI(TAG, "Health check passed");

  return ESP_OK;
}

esp_err_t storage_check_health(void) {
#ifdef VFS_USE_SD_CARD
  if (!vfs_sdcard_try_acquire())
    return ESP_ERR_INVALID_STATE;
#endif
  esp_err_t ret = check_health();
#ifdef VFS_USE_SD_CARD
  vfs_sdcard_release();
#endif
  return ret;
}

const char *storage_get_mount_point(void) {
  return VFS_MOUNT_POINT;
}

const char *storage_get_backend_name(void) {
  return VFS_BACKEND_NAME;
}

void storage_print_info(void) {
  vfs_print_info();
}
