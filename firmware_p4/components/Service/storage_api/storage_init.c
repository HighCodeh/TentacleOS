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

#include "storage_init.h"
#include "vfs_config.h"
#include "vfs_core.h"
#include "esp_log.h"

static const char *TAG = "storage";

extern esp_err_t vfs_init_auto(void);
extern esp_err_t vfs_deinit_auto(void);
extern bool vfs_is_mounted_auto(void);
extern void vfs_print_info(void);

static bool s_initialized = false;

esp_err_t storage_init(void)
{
  if (s_initialized) {
    ESP_LOGW(TAG, "Already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing %s", VFS_BACKEND_NAME);

  esp_err_t ret = vfs_init_auto();
  if (ret == ESP_OK) {
    s_initialized = true;
    ESP_LOGI(TAG, "Storage ready at %s", VFS_MOUNT_POINT);
  } else {
    ESP_LOGE(TAG, "Initialization failed: %s", esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t storage_init_custom(uint8_t max_files, bool format_if_failed)
{
  ESP_LOGW(TAG, "Custom initialization not yet implemented, using defaults");
  return storage_init();
}

esp_err_t storage_deinit(void)
{
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Deinitializing storage");

  esp_err_t ret = vfs_deinit_auto();
  if (ret == ESP_OK) {
    s_initialized = false;
  }

  return ret;
}

bool storage_is_mounted(void)
{
  return s_initialized && vfs_is_mounted_auto();
}

esp_err_t storage_remount(void)
{
  ESP_LOGI(TAG, "Remounting storage");

  if (s_initialized) {
    esp_err_t ret = storage_deinit();
    if (ret != ESP_OK) {
      return ret;
    }
  }

  return storage_init();
}

esp_err_t storage_check_health(void)
{
  if (!storage_is_mounted()) {
    return ESP_ERR_INVALID_STATE;
  }

  const char *test_file = ".health_check";
  const char *test_data = "OK";
  char full_path[256];

  snprintf(full_path, sizeof(full_path), "%s/%s", VFS_MOUNT_POINT, test_file);

  esp_err_t ret = vfs_write_file(full_path, test_data, 2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Health check failed: write error");
    return ret;
  }

  char buffer[10];
  size_t read;
  ret = vfs_read_file(full_path, buffer, sizeof(buffer), &read);
  if (ret != ESP_OK || read != 2) {
    ESP_LOGE(TAG, "Health check failed: read error");
    vfs_unlink(full_path);
    return ESP_FAIL;
  }

  vfs_unlink(full_path);
  ESP_LOGI(TAG, "Health check passed");

  return ESP_OK;
}

const char* storage_get_mount_point(void)
{
  return VFS_MOUNT_POINT;
}

const char* storage_get_backend_name(void)
{
  return VFS_BACKEND_NAME;
}

void storage_print_info(void)
{
  vfs_print_info();
}
