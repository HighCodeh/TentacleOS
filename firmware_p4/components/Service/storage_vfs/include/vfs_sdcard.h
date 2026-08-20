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
 * @file vfs_sdcard.h
 * @brief SD card backend interface
 */
#ifndef VFS_SDCARD_H
#define VFS_SDCARD_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t vfs_sdcard_init(void);
esp_err_t vfs_sdcard_deinit(void);

bool vfs_sdcard_is_mounted(void);
void vfs_sdcard_print_info(void);

/**
 * @brief Copy the mounted card's product name into @p out.
 *
 * @param out  Destination buffer.
 * @param n    Size of @p out in bytes.
 * @return true if the card is mounted and a name was written.
 */
bool vfs_sdcard_get_name(char *out, size_t n);

esp_err_t vfs_sdcard_format(void);
esp_err_t vfs_register_sd_backend(void);
esp_err_t vfs_unregister_sd_backend(void);

/**
 * @brief Give up the app's FAT mount and hand back the SD as a RAW, still-powered
 *        block device for USB Mass Storage. The card is re-initialized (never
 *        formatted). There is no restore path — exit USB-MSC mode via reboot.
 * @param out_card receives an @c sdmmc_card_t* (as @c void*) on success.
 * @return ESP_OK, or an error (the app FAT mount is left detached on failure).
 */
esp_err_t vfs_sdcard_detach_for_msc(void **out_card);

/**
 * @brief Undo vfs_sdcard_detach_for_msc(): release the raw SDMMC card+host that
 *        was handed to USB MSC and remount the app FAT at /sdcard. Call AFTER the
 *        MSC storage layer is torn down. Lets the firmware resume without a reboot.
 * @param card the handle returned by vfs_sdcard_detach_for_msc() (may be NULL).
 */
esp_err_t vfs_sdcard_reattach_after_msc(void *card);

#ifdef __cplusplus
}
#endif

#endif
