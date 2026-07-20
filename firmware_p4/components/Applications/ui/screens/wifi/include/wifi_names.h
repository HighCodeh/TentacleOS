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
 * @file wifi_names.h
 * @brief User-editable list of SSIDs the simulated Wi-Fi scan draws from.
 *
 * Persisted in NVS so edits survive reboots. Seeded with a few realistic
 * defaults on first run. When the list is empty the scan falls back to its
 * built-in name pool.
 */

#ifndef WIFI_NAMES_H
#define WIFI_NAMES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_NAMES_MAX 16 ///< maximum number of stored SSID names
#define WIFI_NAME_LEN  24 ///< max chars (excl. NUL); fits an SSID label row

/** @brief Load from NVS, seeding defaults on first run. Idempotent. */
void wifi_names_init(void);

/**
 * @brief Get the number of stored names.
 * @return Count in the range 0..WIFI_NAMES_MAX.
 */
int wifi_names_count(void);

/**
 * @brief Get the name at a given index.
 * @param index  Zero-based index into the stored list.
 * @return Pointer to the name, or NULL if out of range.
 */
const char *wifi_names_get(int index);

/**
 * @brief Append a name to the list.
 * @param name  Null-terminated name to add.
 * @return true on success, false if the list is full or @p name is empty.
 */
bool wifi_names_add(const char *name);

/**
 * @brief Replace the name at a given index.
 * @param index  Zero-based index into the stored list.
 * @param name   Replacement name; no-op if out of range or empty.
 */
void wifi_names_set(int index, const char *name);

/**
 * @brief Remove the name at a given index.
 * @param index  Zero-based index into the stored list; no-op if out of range.
 */
void wifi_names_remove(int index);

#ifdef __cplusplus
}
#endif

#endif // WIFI_NAMES_H
