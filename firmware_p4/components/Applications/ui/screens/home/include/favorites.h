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

#ifndef FAVORITES_H
#define FAVORITES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "ui_manager.h"

/**
 * @brief Home favorites: a persistent set of app screens the user pinned.
 *
 * Backed by NVS (a bitmask over screen IDs), so it survives reboots and works
 * without an SD card. The Menu toggles entries; the Home renders the pinned apps.
 */

/** @brief True if @p screen is currently a favorite. */
bool favorites_is(screen_id_t screen);

/** @brief Toggle @p screen in/out of the favorites set and persist. */
void favorites_toggle(screen_id_t screen);

/** @brief Number of screens currently favorited. */
int favorites_count(void);

#ifdef __cplusplus
}
#endif

#endif // FAVORITES_H
