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

#include "ota_service.h"
#include "bridge_manager.h"
#include "storage_assets.h"
#include "sd_card_init.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "OTA_SERVICE";

#define OTA_CHUNK_SIZE  4096
#define VERSION_JSON_PATH "config/OTA/firmware.json"

static ota_state_t s_state = OTA_STATE_IDLE;
static char s_current_version[32] = "unknown";

static void load_version_from_assets(void) {
  size_t size;
  uint8_t *json_data = storage_assets_load_file(VERSION_JSON_PATH, &size);
  if (json_data == NULL) {
    ESP_LOGW(TAG, "Could not read firmware.json from assets");
    return;
  }

  cJSON *root = cJSON_ParseWithLength((const char *)json_data, size);
  free(json_data);

  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to parse firmware.json");
    return;
  }

  cJSON *version = cJSON_GetObjectItem(root, "version");
  if (cJSON_IsString(version) && version->valuestring != NULL) {
    strncpy(s_current_version, version->valuestring, sizeof(s_current_version) - 1);
    s_current_version[sizeof(s_current_version) - 1] = '\0';
    ESP_LOGI(TAG, "Current firmware version: %s", s_current_version);
  }

  cJSON_Delete(root);
}

const char* ota_get_current_version(void) {
  if (strcmp(s_current_version, "unknown") == 0) {
    load_version_from_assets();
  }
  return s_current_version;
}

ota_state_t ota_get_state(void) {
  return s_state;
}

bool ota_update_available(void) {
  if (!sd_is_mounted()) {
    return false;
  }

  struct stat st;
  return (stat(OTA_UPDATE_PATH, &st) == 0 && st.st_size > 0);
}

esp_err_t ota_start_update(ota_progress_cb_t progress_cb) {
  if (!ota_update_available()) {
    ESP_LOGE(TAG, "No update file found at %s", OTA_UPDATE_PATH);
    return ESP_ERR_NOT_FOUND;
  }

  // Open update file
  FILE *f = fopen(OTA_UPDATE_PATH, "rb");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open update file");
    return ESP_FAIL;
  }

  // Get file size
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (file_size <= 0) {
    ESP_LOGE(TAG, "Invalid update file size");
    fclose(f);
    return ESP_ERR_INVALID_SIZE;
  }

  ESP_LOGI(TAG, "Update file size: %ld bytes", file_size);

  // Validate: check it fits in the OTA partition
  s_state = OTA_STATE_VALIDATING;
  if (progress_cb) progress_cb(0, "Validating update file...");

  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    ESP_LOGE(TAG, "No OTA partition available");
    fclose(f);
    s_state = OTA_STATE_ERROR;
    return ESP_ERR_NOT_FOUND;
  }

  if (file_size > update_partition->size) {
    ESP_LOGE(TAG, "Update file (%ld) exceeds partition size (%ld)",
             file_size, (long)update_partition->size);
    fclose(f);
    s_state = OTA_STATE_ERROR;
    return ESP_ERR_INVALID_SIZE;
  }

  ESP_LOGI(TAG, "Writing to partition '%s' at offset 0x%lx",
           update_partition->label, (long)update_partition->address);

  // Begin OTA
  s_state = OTA_STATE_WRITING;
  if (progress_cb) progress_cb(5, "Starting OTA write...");

  esp_ota_handle_t ota_handle;
  esp_err_t ret = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
    fclose(f);
    s_state = OTA_STATE_ERROR;
    return ret;
  }

  // Write in chunks
  uint8_t *buffer = malloc(OTA_CHUNK_SIZE);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate read buffer");
    esp_ota_abort(ota_handle);
    fclose(f);
    s_state = OTA_STATE_ERROR;
    return ESP_ERR_NO_MEM;
  }

  long bytes_written = 0;
  int last_percent = 0;

  while (bytes_written < file_size) {
    size_t to_read = (file_size - bytes_written > OTA_CHUNK_SIZE)
      ? OTA_CHUNK_SIZE
      : (size_t)(file_size - bytes_written);

    size_t bytes_read = fread(buffer, 1, to_read, f);
    if (bytes_read == 0) {
      ESP_LOGE(TAG, "Failed to read from update file");
      free(buffer);
      esp_ota_abort(ota_handle);
      fclose(f);
      s_state = OTA_STATE_ERROR;
      return ESP_FAIL;
    }

    ret = esp_ota_write(ota_handle, buffer, bytes_read);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
      free(buffer);
      esp_ota_abort(ota_handle);
      fclose(f);
      s_state = OTA_STATE_ERROR;
      return ret;
    }

    bytes_written += bytes_read;

    // Report progress (5% to 90% range for the write phase)
    int percent = 5 + (int)((bytes_written * 85) / file_size);
    if (percent != last_percent) {
      last_percent = percent;
      if (progress_cb) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Writing: %ld / %ld bytes", bytes_written, file_size);
        progress_cb(percent, msg);
      }
    }
  }

  free(buffer);
  fclose(f);

  ESP_LOGI(TAG, "OTA write complete (%ld bytes)", bytes_written);

  // Finalize OTA
  if (progress_cb) progress_cb(92, "Finalizing update...");

  ret = esp_ota_end(ota_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
    s_state = OTA_STATE_ERROR;
    return ret;
  }

  // Set boot partition
  ret = esp_ota_set_boot_partition(update_partition);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
    s_state = OTA_STATE_ERROR;
    return ret;
  }

  if (progress_cb) progress_cb(95, "Update complete. Rebooting...");

  ESP_LOGI(TAG, "OTA successful. Rebooting into new firmware...");
  s_state = OTA_STATE_REBOOTING;

  // Remove update file so we don't re-apply on next boot
  remove(OTA_UPDATE_PATH);

  esp_restart();

  // Never reached
  return ESP_OK;
}

esp_err_t ota_post_boot_check(void) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == NULL) {
    ESP_LOGE(TAG, "Could not determine running partition");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Running from partition: %s (addr=0x%lx)",
           running->label, (long)running->address);

  // Load version info
  load_version_from_assets();

  // Check if this is a pending OTA that needs confirmation
  esp_ota_img_states_t ota_state;
  esp_err_t ret = esp_ota_get_state_partition(running, &ota_state);

  if (ret == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
    ESP_LOGW(TAG, "New firmware pending verification...");

    // Sync C5 firmware — bridge_manager handles version check + flash
    ret = bridge_manager_init();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "C5 sync failed. Rolling back on next reboot.");
      // Do NOT confirm — bootloader will rollback automatically
      return ESP_FAIL;
    }

    // Everything OK — confirm the update
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "Firmware update confirmed. Version: %s", s_current_version);
  } else {
    ESP_LOGI(TAG, "Normal boot (no pending OTA verification)");
  }

  return ESP_OK;
}
