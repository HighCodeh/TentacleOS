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

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// System power manager. With CONFIG_PM_ENABLE, owns fixed-frequency esp_pm and
// automatic light sleep. The integrated profile keeps PM disabled. Sleep requires
// nobody holds the shared NO_LIGHT_SLEEP lock AND all tasks are blocked, so it
// happens when the device is genuinely idle (screen off, no active resource).
//
// Lives in Service (not Core as the audit suggested) because power_policy lives
// in Applications, which cannot depend on Core (Core already REQUIRES
// Applications - that edge would be a cycle). Both Core (transitively) and
// Applications reach Service.

/**
 * @brief Create and hold a wake guard before configuring fixed-frequency light sleep.
 *
 * Call early in boot; repeated successful initialization is idempotent. The initial
 * guard becomes the USB hold or is released. Failures log and retain pending cleanup
 * for a later init retry. Active resource owners acquire their own balanced holds.
 */
void power_manager_init(void);

/**
 * @brief Hold the CPU out of light sleep. Ref-counted: every acquire needs a
 *        matching release. While the count is > 0 the system never light-sleeps
 *        (frequency remains pinned). Resource owners call this while active.
 */
esp_err_t power_manager_no_sleep_acquire(void);

/** @brief Release one NO_LIGHT_SLEEP hold taken with power_manager_no_sleep_acquire. */
esp_err_t power_manager_no_sleep_release(void);

// USB / external-power state input (item 41). Two independent sources feed it:
//   - VBUS from the charger (battery service) -> power_manager_set_external_power
//   - the TinyUSB bus lifecycle -> power_manager_set_usb_suspended
// While external power is present AND the host has not suspended the bus, the
// manager holds a single NO_LIGHT_SLEEP lock, so the device never light-sleeps
// while plugged in or mid host-link / OTA session. All are no-ops (state is
// still tracked) until CONFIG_PM_ENABLE is turned on.

/** @brief Report external power (USB/charger VBUS) presence. Idempotent. */
void power_manager_set_external_power(bool present);

/** @brief Report USB bus suspend state from tud_suspend/tud_resume (and treated
 *         as suspended on umount). A suspended host drops the plugged-in hold. */
void power_manager_set_usb_suspended(bool suspended);

/** @brief True while external power (VBUS) is present; for battery-vs-plugged
 *         policy decisions. */
bool power_manager_external_power(void);

#ifdef __cplusplus
}
#endif

#endif // POWER_MANAGER_H
