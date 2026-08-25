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

#ifndef CONFIG_SYNC_H
#define CONFIG_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

// P4 side of the P4->C5 config cache. The P4 owns the master config files (in
// its assets partition) and pushes each to the C5 only when its CRC32 differs
// from the hash the C5 has stored, so unchanged files are never re-sent.

/**
 * @brief Kick off a background task that waits for the C5 to answer, then syncs
 * every registered config to it. Non-blocking; safe to call once at boot after
 * bridge_manager_init().
 */
void config_sync_start(void);

/**
 * @brief Push one config id to the C5 now if its hash differs (blocking).
 * Call after editing a master config file so the change reaches the C5 without
 * waiting for the next boot. Returns ESP_OK if the C5 is up to date or updated.
 */
esp_err_t config_sync_push(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_SYNC_H
