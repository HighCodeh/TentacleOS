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

#ifndef MESHCORE_NVS_H
#define MESHCORE_NVS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#define MC_NVS_NAMESPACE "meshcore"

/**
 * @brief Initializes NVS flash (idempotent).
 *
 * Erases and recreates the partition if it detects an incompatible version or full state.
 *
 * @return
 *   - ESP_OK on success
 *   - esp_nvs error code on permanent failure
 */
esp_err_t mc_nvs_init(void);

/**
 * @brief Reads a persisted blob.
 *
 * @param[in]     key  Key (max 15 chars, within the namespace).
 * @param[out]    out  Output buffer.
 * @param[in,out] len  IN: buffer size. OUT: bytes read.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NVS_NOT_FOUND if key does not exist
 */
esp_err_t mc_nvs_get_blob(const char *key, void *out, size_t *len);

/**
 * @brief Writes a persistent blob. Commits immediately.
 *
 * @param key  Key.
 * @param in   Input buffer.
 * @param len  Size in bytes.
 * @return ESP_OK on success.
 */
esp_err_t mc_nvs_set_blob(const char *key, const void *in, size_t len);

/**
 * @brief Reads a persisted u32.
 *
 * @param[in]  key  Key.
 * @param[out] out  Value read.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NVS_NOT_FOUND if key does not exist
 */
esp_err_t mc_nvs_get_u32(const char *key, uint32_t *out);

/**
 * @brief Writes a u32. Commits immediately.
 *
 * @param key  Key.
 * @param val  Value.
 * @return ESP_OK on success.
 */
esp_err_t mc_nvs_set_u32(const char *key, uint32_t val);

/**
 * @brief Reads a persisted string.
 *
 * @param[in]  key  Key.
 * @param[out] out  Output buffer.
 * @param[in]  max  Buffer size (includes terminator).
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NVS_NOT_FOUND if key does not exist
 */
esp_err_t mc_nvs_get_str(const char *key, char *out, size_t max);

/**
 * @brief Writes a null-terminated string.
 *
 * @param key  Key.
 * @param val  String.
 * @return ESP_OK on success.
 */
esp_err_t mc_nvs_set_str(const char *key, const char *val);

/**
 * @brief Erases a key. Silent if it does not exist.
 *
 * @param key  Key.
 * @return ESP_OK whenever the operation finishes (even if key absent).
 */
esp_err_t mc_nvs_erase(const char *key);

#ifdef __cplusplus
}
#endif

#endif // MESHCORE_NVS_H
