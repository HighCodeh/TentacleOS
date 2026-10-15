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
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the global power policy (idempotent). Call once from ui_init(),
 *        under the LVGL lock (it creates an lv_timer).
 *
 * Owns the low-battery safety policy and the global manual power-off gestures.

 */
void power_policy_init(void);

/**
 * @brief Open the safe power-off confirmation dialog.
 *
 * Cancel is
 * selected by default. Confirmation stops the active operation,
 * requests
 * BQ25896 ship mode, unmounts storage, and presents the final power
 * state.
 * Must be called from the LVGL thread.
 */
void power_policy_show_shutdown_dialog(void);

/**
 * @brief Whether the screen is currently asleep (panel off).
 *
 * The input router swallows input while asleep so the wake press only wakes the
 * screen instead of also acting on the underlying UI.
 */
bool power_policy_is_asleep(void);

/**
 * @brief Keep the display awake only while owner is the active LVGL screen.
 *
 * Call under
 * the LVGL lock. Enable while an animation is running; disable when
 * paused or leaving its
 * screen. Disabling a different owner does nothing.
 * The hold is also released when owner is
 * deleted. This does not synthesize
 * input, change the saved timeout/brightness, or suspend
 * battery/manual OFF
 * policy. A NULL or invalid owner cannot acquire the hold.
 */
void power_policy_set_display_hold(const lv_obj_t *owner, bool enabled);

#ifdef __cplusplus
}
#endif

#endif // POWER_POLICY_H
