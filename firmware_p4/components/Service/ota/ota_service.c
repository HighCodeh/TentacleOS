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

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "bridge_manager.h"
#include "cJSON.h"
#include "ota_version.h"
#include "sd_card_init.h"
#include "storage_assets.h"

static const char *TAG = "OTA_SERVICE";

#define OTA_CHUNK_SIZE       4096
#define VERSION_JSON_PATH    "config/OTA/firmware.json"
#define PROGRESS_MSG_SIZE    48
#define PROGRESS_WRITE_START 5
#define PROGRESS_WRITE_RANGE 85
#define PROGRESS_FINALIZE    92
#define PROGRESS_REBOOT      95

static ota_state_t s_state = OTA_STATE_IDLE;

const char *ota_get_current_version(void) {
  return FIRMWARE_VERSION;
}

ota_state_t ota_get_state(void) {
  return s_state;
}

static void sync_version_to_assets(void) {
  size_t size;
  uint8_t *json_data = storage_assets_load_file(VERSION_JSON_PATH, &size);
  if (json_data == NULL) {
    return;
  }

  cJSON *root = cJSON_ParseWithLength((const char *)json_data, size);
  free(json_data);
  if (root == NULL) {
    return;
  }

  cJSON *version = cJSON_GetObjectItem(root, "version");
  if (cJSON_IsString(version) && strcmp(version->valuestring, FIRMWARE_VERSION) != 0) {
    cJSON_SetValuestring(version, FIRMWARE_VERSION);
    char *updated_json = cJSON_PrintUnformatted(root);
    if (updated_json != NULL) {
      storage_assets_write_file(VERSION_JSON_PATH, updated_json);
      ESP_LOGI(TAG, "Updated firmware.json to v%s", FIRMWARE_VERSION);
      free(updated_json);
    }
  }

  cJSON_Delete(root);
}

bool ota_update_available(void) {
  if (!sd_is_mounted()) {
    return false;
  }

  struct stat st;
  return (stat(OTA_UPDATE_PATH, &st) == 0 && st.st_size > 0);
}

esp_err_t ota_start_update(ota_progress_cb_t progress_cb) {
  esp_err_t ret = ESP_OK;
  FILE *f = NULL;
  uint8_t *buffer = NULL;
  esp_ota_handle_t ota_handle = 0;
  bool ota_begun = false;

  if (!ota_update_available()) {
    ESP_LOGE(TAG, "No update file found at %s", OTA_UPDATE_PATH);
    return ESP_ERR_NOT_FOUND;
  }

  f = fopen(OTA_UPDATE_PATH, "rb");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open update file");
    return ESP_FAIL;
  }

  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (file_size <= 0) {
    ESP_LOGE(TAG, "Invalid update file size");
    ret = ESP_ERR_INVALID_SIZE;
    goto cleanup;
  }

  ESP_LOGI(TAG, "Update file size: %ld bytes", file_size);

  // Validate partition
  s_state = OTA_STATE_VALIDATING;
  if (progress_cb != NULL) {
    progress_cb(0, "Validating update file...");
  }

  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    ESP_LOGE(TAG, "No OTA partition available");
    ret = ESP_ERR_NOT_FOUND;
    goto cleanup;
  }

  if (file_size > (long)update_partition->size) {
    ESP_LOGE(TAG,
             "Update file (%ld) exceeds partition size (%ld)",
             file_size,
             (long)update_partition->size);
    ret = ESP_ERR_INVALID_SIZE;
    goto cleanup;
  }

  ESP_LOGI(TAG,
           "Writing to partition '%s' at offset 0x%lx",
           update_partition->label,
           (long)update_partition->address);

  // Begin OTA write
  s_state = OTA_STATE_WRITING;
  if (progress_cb != NULL) {
    progress_cb(PROGRESS_WRITE_START, "Starting OTA write...");
  }

  ret = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
    goto cleanup;
  }
  ota_begun = true;

  buffer = malloc(OTA_CHUNK_SIZE);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate read buffer");
    ret = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  // Write in chunks
  long bytes_written = 0;
  int last_percent = 0;

  while (bytes_written < file_size) {
    size_t to_read = (file_size - bytes_written > OTA_CHUNK_SIZE)
                         ? OTA_CHUNK_SIZE
                         : (size_t)(file_size - bytes_written);

    size_t bytes_read = fread(buffer, 1, to_read, f);
    if (bytes_read == 0) {
      ESP_LOGE(TAG, "Failed to read from update file");
      ret = ESP_FAIL;
      goto cleanup;
    }

    ret = esp_ota_write(ota_handle, buffer, bytes_read);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
      goto cleanup;
    }

    bytes_written += bytes_read;

    int percent = PROGRESS_WRITE_START + (int)((bytes_written * PROGRESS_WRITE_RANGE) / file_size);
    if (percent != last_percent && progress_cb != NULL) {
      last_percent = percent;
      char msg[PROGRESS_MSG_SIZE];
      snprintf(msg, sizeof(msg), "Writing: %ld / %ld bytes", bytes_written, file_size);
      progress_cb(percent, msg);
    }
  }

  ESP_LOGI(TAG, "OTA write complete (%ld bytes)", bytes_written);

  // Finalize
  if (progress_cb != NULL) {
    progress_cb(PROGRESS_FINALIZE, "Finalizing update...");
  }

  ret = esp_ota_end(ota_handle);
  ota_begun = false; // Already ended, don't abort in cleanup
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
    goto cleanup;
  }

  ret = esp_ota_set_boot_partition(update_partition);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
    goto cleanup;
  }

  if (progress_cb != NULL) {
    progress_cb(PROGRESS_REBOOT, "Update complete. Rebooting...");
  }

  ESP_LOGI(TAG, "OTA successful. Rebooting...");
  s_state = OTA_STATE_REBOOTING;

  free(buffer);
  fclose(f);
  remove(OTA_UPDATE_PATH);
  esp_restart();

  return ESP_OK; // Never reached

cleanup:
  s_state = OTA_STATE_ERROR;
  if (ota_begun) {
    esp_ota_abort(ota_handle);
  }
  free(buffer);
  if (f != NULL) {
    fclose(f);
  }
  return ret;
}

esp_err_t ota_post_boot_check(void) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == NULL) {
    ESP_LOGE(TAG, "Could not determine running partition");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Running from partition: %s (addr=0x%lx)", running->label, (long)running->address);

  esp_ota_img_states_t ota_state;
  esp_err_t ret = esp_ota_get_state_partition(running, &ota_state);

  if (ret == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
    ESP_LOGW(TAG, "New firmware pending verification...");

    ret = bridge_manager_init();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "C5 sync failed — rollback will occur on next reboot");
      return ESP_FAIL;
    }

    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "Firmware update confirmed: v%s", FIRMWARE_VERSION);
  } else {
    ESP_LOGI(TAG, "Normal boot (no pending OTA verification)");
  }

  sync_version_to_assets();
  return ESP_OK;
}
