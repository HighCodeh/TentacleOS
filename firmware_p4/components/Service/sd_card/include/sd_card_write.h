// SPDX-License-Identifier: GPL-3.0-or-later
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
 * @file sd_card_write.h
 * @brief SD card write operations.
 */

#ifndef SD_CARD_WRITE_H
#define SD_CARD_WRITE_H

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write string to file (overwrites).
 * @param path File path.
 * @param data String to write.
 * @return ESP_OK on success.
 */
esp_err_t sd_write_string(const char *path, const char *data);

/**
 * @brief Append string to file.
 * @param path File path.
 * @param data String to append.
 * @return ESP_OK on success.
 */
esp_err_t sd_append_string(const char *path, const char *data);

/**
 * @brief Write binary data.
 * @param path File path.
 * @param data Data buffer.
 * @param size Size in bytes.
 * @return ESP_OK on success.
 */
esp_err_t sd_write_binary(const char *path, const void *data, size_t size);

/**
 * @brief Append binary data.
 * @param path File path.
 * @param data Data buffer.
 * @param size Size in bytes.
 * @return ESP_OK on success.
 */
esp_err_t sd_append_binary(const char *path, const void *data, size_t size);

/**
 * @brief Write a line (appends newline).
 * @param path File path.
 * @param line Line to write.
 * @return ESP_OK on success.
 */
esp_err_t sd_write_line(const char *path, const char *line);

/**
 * @brief Append a line (appends newline).
 * @param path File path.
 * @param line Line to append.
 * @return ESP_OK on success.
 */
esp_err_t sd_append_line(const char *path, const char *line);

/**
 * @brief Write formatted data (printf-style).
 * @param path File path.
 * @param format Format string.
 * @param ... Arguments.
 * @return ESP_OK on success.
 */
esp_err_t sd_write_formatted(const char *path, const char *format, ...);

/**
 * @brief Append formatted data (printf-style).
 * @param path File path.
 * @param format Format string.
 * @param ... Arguments.
 * @return ESP_OK on success.
 */
esp_err_t sd_append_formatted(const char *path, const char *format, ...);

/**
 * @brief Write buffer to file.
 * @param path File path.
 * @param buffer Data buffer.
 * @param size Buffer size.
 * @return ESP_OK on success.
 */
esp_err_t sd_write_buffer(const char *path, const void *buffer, size_t size);

/**
 * @brief Write byte array.
 * @param path File path.
 * @param bytes Byte array.
 * @param count Number of bytes.
 * @return ESP_OK on success.
 */
esp_err_t sd_write_bytes(const char *path, const uint8_t *bytes, size_t count);

/**
 * @brief Write a single byte.
 * @param path File path.
 * @param byte Byte to write.
 * @return ESP_OK on success.
 */
esp_err_t sd_write_byte(const char *path, uint8_t byte);

/**
 * @brief Write integer to file.
 * @param path File path.
 * @param value Integer value.
 * @return ESP_OK on success.
 */
esp_err_t sd_write_int(const char *path, int32_t value);

/**
 * @brief Write float to file.
 * @param path File path.
 * @param value Float value.
 * @return ESP_OK on success.
 */
esp_err_t sd_write_float(const char *path, float value);

/**
 * @brief Write CSV row (overwrites).
 * @param path File path.
 * @param columns Array of column strings.
 * @param num_columns Number of columns.
 * @return ESP_OK on success.
 */
esp_err_t sd_write_csv_row(const char *path, const char **columns, size_t num_columns);

/**
 * @brief Append CSV row.
 * @param path File path.
 * @param columns Array of column strings.
 * @param num_columns Number of columns.
 * @return ESP_OK on success.
 */
esp_err_t sd_append_csv_row(const char *path, const char **columns, size_t num_columns);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_WRITE_H */