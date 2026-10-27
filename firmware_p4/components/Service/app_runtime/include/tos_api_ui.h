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

// Firmware-internal hook for the UI subsystem (tos_api_ui.c). NOT part of the
// app-facing ABI.

#ifndef TOS_API_UI_H
#define TOS_API_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reclaim any on-device screen an app created, keyed by its task.
 *
 * Safe to call for a non-UI app (no-op). Deletes the app's LVGL screen, clears
 * its input hook and invalidates its widget handles, then returns the display to
 * the Apps launcher. Called from the app manager's finish_slot() on both natural
 * exit and force-kill, so a killed app never leaves an orphaned screen.
 */
void tos_ui_app_teardown(void *task);

#ifdef __cplusplus
}
#endif

#endif // TOS_API_UI_H
