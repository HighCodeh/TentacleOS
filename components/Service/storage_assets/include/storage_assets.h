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

#ifndef STORAGE_ASSETS_H
#define STORAGE_ASSETS_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the assets partition (LittleFS)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t storage_assets_init(void);

/**
 * @brief Deinitialize and unmount the assets partition
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t storage_assets_deinit(void);

/**
 * @brief Check if assets partition is mounted
 * 
 * @return true if mounted, false otherwise
 */
bool storage_assets_is_mounted(void);

/**
 * @brief Read a file from the assets partition
 * 
 * @param filename File name (e.g. "dvd_logo_48x24.bin")
 * @param buffer Pointer to buffer where data will be stored
 * @param size Maximum size to read
 * @param out_read Pointer to store actual bytes read (can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t storage_assets_read_file(const char *filename, uint8_t *buffer, size_t size, size_t *out_read);

/**
 * @brief Get the size of a file in the assets partition
 * 
 * @param filename File name
 * @param out_size Pointer to store file size
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t storage_assets_get_file_size(const char *filename, size_t *out_size);

/**
 * @brief Allocate memory and load complete file from assets partition
 * 
 * @note Caller must free() the returned pointer after use
 * 
 * @param filename File name
 * @param out_size Pointer to store file size (can be NULL)
 * @return Pointer to allocated data, or NULL on error
 */
uint8_t* storage_assets_load_file(const char *filename, size_t *out_size);

/**
 * @brief Get the mount point of the assets partition
 * 
 * @return Mount point path string
 */
const char* storage_assets_get_mount_point(void);

/**
 * @brief Print information about the assets partition
 */
void storage_assets_print_info(void);

esp_err_t storage_assets_write_file(const char *filename, const char *data);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_ASSETS_H