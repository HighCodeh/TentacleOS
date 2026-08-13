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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the global power policy (idempotent). Call once from ui_init(),
 *        under the LVGL lock (it creates an lv_timer).
 *
 * Owns two always-on behaviours in a single LVGL timer:
 *  - Rest screen: after a period of button inactivity, cover the UI with a dark
 *    top-layer overlay (the backlight cannot be switched on this prototype) and
 *    dismiss it (swallowing the wake press) on the next press.
 *  - Low-battery policy: raise a toast on the low-battery edge.
 */
void power_policy_init(void);

#ifdef __cplusplus
}
#endif

#endif // POWER_POLICY_H
