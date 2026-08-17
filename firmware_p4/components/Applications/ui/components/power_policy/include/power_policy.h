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

#ifndef POWER_POLICY_H
#define POWER_POLICY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the global power policy (idempotent). Call once from ui_init(),
 *        under the LVGL lock (it creates an lv_timer).
 *
 * Owns two always-on behaviours in a single LVGL timer:
 *  - Screen power: after auto_lock_seconds of button inactivity it dims (when
 *    auto_dim is set) and then sleeps the panel, restoring the user's brightness
 *    on the next input. Reads input_last_activity_ms() and g_config_screen.
 *  - Low-battery policy: raise a toast on the low-battery edge.
 */
void power_policy_init(void);

/**
 * @brief Whether the screen is currently asleep (panel off).
 *
 * The input router swallows input while asleep so the wake press only wakes the
 * screen instead of also acting on the underlying UI.
 */
bool power_policy_is_asleep(void);

#ifdef __cplusplus
}
#endif

#endif // POWER_POLICY_H
