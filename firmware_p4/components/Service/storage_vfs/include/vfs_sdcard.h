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

#ifdef __cplusplus
}
#endif

#endif
