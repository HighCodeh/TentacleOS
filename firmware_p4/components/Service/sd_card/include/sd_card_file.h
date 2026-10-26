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

/**
 * @file sd_card_file.h
 * @brief File management operations.
 */

#ifndef SD_CARD_FILE_H
#define SD_CARD_FILE_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief File information.
 */
typedef struct {
  char path[256];
  size_t size;
  time_t modified_time;
  bool is_directory;
} sd_file_info_t;

/**
 * @brief Check if a file exists.
 * @param path File path.
 * @return true if exists.
 */
bool sd_file_exists(const char *path);

/**
 * @brief Delete a file.
 * @param path File path.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_delete(const char *path);

/**
 * @brief Rename or move a file.
 * @param old_path Old path.
 * @param new_path New path.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_rename(const char *old_path, const char *new_path);

/**
 * @brief Copy a file.
 * @param src_path Source path.
 * @param dst_path Destination path.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_copy(const char *src_path, const char *dst_path);

/**
 * @brief Get file size.
 * @param path File path.
 * @param size Pointer to receive size.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_get_size(const char *path, size_t *size);

/**
 * @brief Get file information.
 * @param path File path.
 * @param info Struct to receive info.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_get_info(const char *path, sd_file_info_t *info);

/**
 * @brief Truncate file to a specific size.
 * @param path File path.
 * @param size New size.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_truncate(const char *path, size_t size);

/**
 * @brief Check if a file is empty.
 * @param path File path.
 * @param is_empty Pointer to receive result.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_is_empty(const char *path, bool *is_empty);

/**
 * @brief Move a file.
 * @param src_path Source path.
 * @param dst_path Destination path.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_move(const char *src_path, const char *dst_path);

/**
 * @brief Compare two files byte-by-byte.
 * @param path1 First file path.
 * @param path2 Second file path.
 * @param are_equal Pointer to receive result.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_compare(const char *path1, const char *path2, bool *are_equal);

/**
 * @brief Clear file contents (truncate to zero).
 * @param path File path.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_clear(const char *path);

/**
 * @brief Get file extension.
 * @param path File path.
 * @param extension Buffer to receive extension.
 * @param size Buffer size.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_get_extension(const char *path, char *extension, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_FILE_H */