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

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"

#define OTA_UPDATE_PATH "/sdcard/update/tentacleos.bin"

/**
 * @brief OTA state machine states.
 */
typedef enum {
  OTA_STATE_IDLE = 0,
  OTA_STATE_VALIDATING,
  OTA_STATE_WRITING,
  OTA_STATE_REBOOTING,
  OTA_STATE_ERROR,
  OTA_STATE_COUNT
} ota_state_t;

/**
 * @brief Progress callback invoked during OTA update phases.
 *
 * @param percent  Progress percentage (0-100).
 * @param message  Human-readable status message.
 */
typedef void (*ota_progress_cb_t)(int percent, const char *message);

/**
 * @brief Check if an update file exists on the SD card.
 *
 * @return true if OTA_UPDATE_PATH exists and has non-zero size.
 */
bool ota_update_available(void);

/**
 * @brief Start the OTA update process from the SD card.
 *
 * Reads the firmware binary from OTA_UPDATE_PATH, validates it against
 * the target OTA partition size, writes it in chunks, sets the boot
 * partition, removes the update file, and reboots.
 *
 * This function does not return on success (calls esp_restart).
 *
 * @param progress_cb  Optional callback for progress reporting (NULL to disable).
 * @return
 *   - ESP_ERR_NOT_FOUND if no update file exists
 *   - ESP_ERR_INVALID_SIZE if the file exceeds partition size
 *   - ESP_ERR_NO_MEM if buffer allocation fails
 *   - ESP_FAIL on read/write errors
 */
esp_err_t ota_start_update(ota_progress_cb_t progress_cb);

/**
 * @brief Post-boot verification for newly flashed firmware.
 *
 * If the running partition is pending OTA verification, this function
 * syncs the C5 co-processor firmware via bridge_manager and confirms
 * the update. If C5 sync fails, the update is NOT confirmed and the
 * bootloader will rollback on next reboot.
 *
 * Also syncs firmware.json in the assets partition to match the
 * running firmware version.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_FAIL if C5 sync or partition check fails
 */
esp_err_t ota_post_boot_check(void);

/**
 * @brief Get the current firmware version string.
 *
 * @return Null-terminated version string (e.g. "1.2.3").
 */
const char *ota_get_current_version(void);

/**
 * @brief Get the current OTA state machine state.
 *
 * @return Current ota_state_t value.
 */
ota_state_t ota_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_SERVICE_H
