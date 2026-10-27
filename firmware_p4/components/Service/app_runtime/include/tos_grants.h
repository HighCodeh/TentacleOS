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

// Persisted capability grants: which capabilities the user approved for an app,
// keyed by app name, stored in NVS so consent is asked once.

#ifndef TOS_GRANTS_H
#define TOS_GRANTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

/** @brief Capabilities approved for @p name, or 0 if none stored. */
uint32_t tos_grants_get(const char *name);

/** @brief Persist the approved capability set for @p name. */
esp_err_t tos_grants_set(const char *name, uint32_t caps);

/** @brief Remove the stored grant for @p name. */
esp_err_t tos_grants_clear(const char *name);

/**
 * @brief List stored grants into @p names / @p caps (up to @p max).
 * @return the number of grants stored.
 */
int tos_grants_list(char names[][16], uint32_t *caps, int max);

#ifdef __cplusplus
}
#endif

#endif // TOS_GRANTS_H
