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

#ifndef ASSETS_MANAGER_H
#define ASSETS_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "lvgl.h"

/** @brief Initialize the assets manager and load assets from flash. */
void assets_manager_init(void);

/** @brief Get a cached asset image descriptor by path. */
lv_image_dsc_t *assets_get(const char *path);

/** @brief Free all loaded assets and release memory. */
void assets_manager_free_all(void);

/**
 * @brief Drop the LVGL image cache under memory pressure (safe mid-session).
 *
 * Frees the decoded-pixel pool; asset nodes and handed-out descriptors stay
 * valid, so a later redraw re-decodes from flash. Used by the heap policy.
 */
void assets_manager_evict_cache(void);

/** @brief Load assets from SD card directory, overriding flash assets. */
int assets_load_from_sd(const char *sd_dir, const char *flash_prefix);

/** @brief Unload all SD-sourced assets from the cache. */
void assets_unload_sd(void);

#ifdef __cplusplus
}
#endif

#endif // ASSETS_MANAGER_H