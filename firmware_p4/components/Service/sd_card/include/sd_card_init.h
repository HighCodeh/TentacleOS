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
 * @file sd_card_init.h
 * @brief SD card initialization and control (SDMMC 4-bit).
 */

#ifndef SD_CARD_INIT_H
#define SD_CARD_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "vfs_config.h"

#define SD_MAX_FILES       10
#define SD_ALLOCATION_UNIT (16 * 1024)

/**
 * @brief Initialize SD card with default settings (SDMMC 4-bit).
 *
 * @return ESP_OK on success.
 */
esp_err_t sd_init(void);

/**
 * @brief Initialize SD card with custom settings.
 *
 * @param max_files          Maximum open files.
 * @param is_format_on_fail  Format partition if mount fails.
 * @return ESP_OK on success.
 */
esp_err_t sd_init_custom(uint8_t max_files, bool is_format_on_fail);

/**
 * @brief Unmount the SD card and free resources.
 *
 * @return ESP_OK on success.
 */
esp_err_t sd_deinit(void);

/**
 * @brief Check if the SD card is currently mounted.
 *
 * @return true if mounted.
 */
bool sd_is_mounted(void);

/**
 * @brief Remount the SD card (deinit + init).
 *
 * @return ESP_OK on success.
 */
esp_err_t sd_remount(void);

/**
 * @brief Perform a write/read health check.
 *
 * @return ESP_OK if healthy.
 */
esp_err_t sd_check_health(void);

#ifdef __cplusplus
}
#endif

#endif // SD_CARD_INIT_H
