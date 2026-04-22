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

#ifndef MESHTASTIC_NVS_H
#define MESHTASTIC_NVS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#define MT_NVS_VERSION 1

/**
 * @brief Initialize the Meshtastic NVS subsystem.
 *
 * Opens NVS namespace "meshtastic". If the stored schema version does
 * not match MT_NVS_VERSION, erases every key in the namespace and
 * writes the current version (simple migration via full reset).
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NO_MEM if NVS allocation fails
 *   - other esp_err_t from nvs_flash_init on fatal errors
 */
esp_err_t mt_nvs_init(void);

/**
 * @brief Store a binary blob under a key.
 *
 * Commits pending writes immediately on success.
 *
 * @param key   Null-terminated key. Max 15 chars per NVS rules.
 * @param data  Pointer to payload. Must not be NULL if len > 0.
 * @param len   Payload length in bytes.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if the subsystem was not initialized
 *   - other esp_err_t from NVS backend
 */
esp_err_t mt_nvs_set_blob(const char *key, const void *data, size_t len);

/**
 * @brief Read a binary blob from a key.
 *
 * @param key         Null-terminated key.
 * @param out_data    Destination buffer. Must not be NULL if max_len > 0.
 * @param max_len     Capacity of out_data in bytes.
 * @return Number of bytes read. 0 if the key is absent. Negative on error.
 */
int mt_nvs_get_blob(const char *key, void *out_data, size_t max_len);

/**
 * @brief Store a uint32 value under a key.
 */
esp_err_t mt_nvs_set_u32(const char *key, uint32_t value);

/**
 * @brief Read a uint32 value. Returns default_value if the key is absent.
 */
uint32_t mt_nvs_get_u32(const char *key, uint32_t default_value);

/**
 * @brief Erase a single key.
 */
esp_err_t mt_nvs_erase(const char *key);

/**
 * @brief Erase every key in the namespace. Intended for factory reset.
 */
esp_err_t mt_nvs_factory_reset(void);

/**
 * @brief Force flush of any pending NVS writes to flash.
 */
esp_err_t mt_nvs_commit(void);

#ifdef __cplusplus
}
#endif

#endif // MESHTASTIC_NVS_H
