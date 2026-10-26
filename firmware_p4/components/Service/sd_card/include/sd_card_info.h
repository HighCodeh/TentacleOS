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
 * @file sd_card_info.h
 * @brief SD card information queries.
 */

#ifndef SD_CARD_INFO_H
#define SD_CARD_INFO_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SD card hardware information.
 */
typedef struct {
  char name[16];
  uint32_t capacity_mb;
  uint32_t sector_size;
  uint32_t num_sectors;
  uint32_t speed_khz;
  uint8_t card_type;
  bool is_mounted;
} sd_card_info_t;

/**
 * @brief Filesystem statistics.
 */
typedef struct {
  uint64_t total_bytes;
  uint64_t used_bytes;
  uint64_t free_bytes;
} sd_fs_stats_t;

/**
 * @brief Get card hardware information.
 * @param info Struct to receive info.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_card_info(sd_card_info_t *info);

/**
 * @brief Print card info to console.
 */
void sd_print_card_info(void);

/**
 * @brief Get filesystem statistics.
 * @param stats Struct to receive stats.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_fs_stats(sd_fs_stats_t *stats);

/**
 * @brief Get free space.
 * @param free_bytes Pointer to receive free bytes.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_free_space(uint64_t *free_bytes);

/**
 * @brief Get total space.
 * @param total_bytes Pointer to receive total bytes.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_total_space(uint64_t *total_bytes);

/**
 * @brief Get used space.
 * @param used_bytes Pointer to receive used bytes.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_used_space(uint64_t *used_bytes);

/**
 * @brief Calculate usage percentage.
 * @param percentage Pointer to receive percentage.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_usage_percent(float *percentage);

/**
 * @brief Get card name.
 * @param name Buffer to receive name.
 * @param size Buffer size.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_card_name(char *name, size_t size);

/**
 * @brief Get card capacity in MB.
 * @param capacity_mb Pointer to receive capacity.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_capacity(uint32_t *capacity_mb);

/**
 * @brief Get card speed.
 * @param speed_khz Pointer to receive speed.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_speed(uint32_t *speed_khz);

/**
 * @brief Get card type.
 * @param type Pointer to receive type.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_card_type(uint8_t *type);

/**
 * @brief Get card type name string.
 * @param type_name Buffer to receive name.
 * @param size Buffer size.
 * @return ESP_OK on success.
 */
esp_err_t sd_get_card_type_name(char *type_name, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_INFO_H */