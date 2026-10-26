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
 * @file sd_card_read.h
 * @brief SD card read operations.
 */
#ifndef SD_CARD_READ_H
#define SD_CARD_READ_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback invoked for each line.
 */
typedef void (*sd_line_callback_t)(const char *line, void *user_data);

/**
 * @brief Read file as null-terminated string.
 * @param path File path.
 * @param buffer Buffer to receive data.
 * @param buffer_size Buffer size.
 * @return ESP_OK on success.
 */
esp_err_t sd_read_string(const char *path, char *buffer, size_t buffer_size);

/**
 * @brief Read binary data.
 * @param path File path.
 * @param buffer Buffer to receive data.
 * @param size Maximum bytes to read.
 * @param bytes_read Pointer to receive bytes read.
 * @return ESP_OK on success.
 */
esp_err_t sd_read_binary(const char *path, void *buffer, size_t size, size_t *bytes_read);

/**
 * @brief Read a specific line by number.
 * @param path File path.
 * @param buffer Buffer to receive line.
 * @param buffer_size Buffer size.
 * @param line_number Line number (1-based).
 * @return ESP_OK on success.
 */
esp_err_t sd_read_line(const char *path, char *buffer, size_t buffer_size, uint32_t line_number);

/**
 * @brief Read all lines via callback.
 * @param path File path.
 * @param callback Callback function.
 * @param user_data User context data.
 * @return ESP_OK on success.
 */
esp_err_t sd_read_lines(const char *path, sd_line_callback_t callback, void *user_data);

/**
 * @brief Count number of lines.
 * @param path File path.
 * @param line_count Pointer to receive count.
 * @return ESP_OK on success.
 */
esp_err_t sd_count_lines(const char *path, uint32_t *line_count);

/**
 * @brief Read a chunk of data at offset.
 * @param path File path.
 * @param offset Starting offset.
 * @param buffer Buffer to receive data.
 * @param size Maximum bytes to read.
 * @param bytes_read Pointer to receive bytes read.
 * @return ESP_OK on success.
 */
esp_err_t
sd_read_chunk(const char *path, size_t offset, void *buffer, size_t size, size_t *bytes_read);

/**
 * @brief Read the first line.
 * @param path File path.
 * @param buffer Buffer to receive line.
 * @param buffer_size Buffer size.
 * @return ESP_OK on success.
 */
esp_err_t sd_read_first_line(const char *path, char *buffer, size_t buffer_size);

/**
 * @brief Read the last line.
 * @param path File path.
 * @param buffer Buffer to receive line.
 * @param buffer_size Buffer size.
 * @return ESP_OK on success.
 */
esp_err_t sd_read_last_line(const char *path, char *buffer, size_t buffer_size);

/**
 * @brief Read an integer value.
 * @param path File path.
 * @param value Pointer to receive value.
 * @return ESP_OK on success.
 */
esp_err_t sd_read_int(const char *path, int32_t *value);

/**
 * @brief Read a float value.
 * @param path File path.
 * @param value Pointer to receive value.
 * @return ESP_OK on success.
 */
esp_err_t sd_read_float(const char *path, float *value);

/**
 * @brief Read bytes.
 * @param path File path.
 * @param bytes Buffer to receive bytes.
 * @param max_count Maximum bytes.
 * @param count Pointer to receive count.
 * @return ESP_OK on success.
 */
esp_err_t sd_read_bytes(const char *path, uint8_t *bytes, size_t max_count, size_t *count);

/**
 * @brief Read a single byte.
 * @param path File path.
 * @param byte Pointer to receive byte.
 * @return ESP_OK on success.
 */
esp_err_t sd_read_byte(const char *path, uint8_t *byte);

/**
 * @brief Check if file contains a string.
 * @param path File path.
 * @param search Search string.
 * @param found Pointer to receive result.
 * @return ESP_OK on success.
 */
esp_err_t sd_file_contains(const char *path, const char *search, bool *found);

/**
 * @brief Count occurrences of a string.
 * @param path File path.
 * @param search Search string.
 * @param count Pointer to receive count.
 * @return ESP_OK on success.
 */
esp_err_t sd_count_occurrences(const char *path, const char *search, uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_READ_H */