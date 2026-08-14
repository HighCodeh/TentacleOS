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

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// System power manager. Owns esp_pm: dynamic frequency scaling (CPU drops clock
// when idle) plus automatic light sleep. Light sleep only actually engages when
// nobody holds the shared NO_LIGHT_SLEEP lock AND all tasks are blocked, so it
// happens when the device is genuinely idle (screen off, no active resource).
//
// Lives in Service (not Core as the audit suggested) because power_policy lives
// in Applications, which cannot depend on Core (Core already REQUIRES
// Applications - that edge would be a cycle). Both Core (transitively) and
// Applications reach Service.

/**
 * @brief Configure esp_pm (DFS + light sleep) and create the shared lock.
 *
 * Call once early in boot. Does not acquire the lock; owners that need the CPU
 * fully awake acquire it themselves (screen on, USB session, active radio, ...).
 */
void power_manager_init(void);

/**
 * @brief Hold the CPU out of light sleep. Ref-counted: every acquire needs a
 *        matching release. While the count is > 0 the system never light-sleeps
 *        (DFS still scales frequency). Resource owners call this while active.
 */
esp_err_t power_manager_no_sleep_acquire(void);

/** @brief Release one NO_LIGHT_SLEEP hold taken with power_manager_no_sleep_acquire. */
esp_err_t power_manager_no_sleep_release(void);

#ifdef __cplusplus
}
#endif

#endif // POWER_MANAGER_H
