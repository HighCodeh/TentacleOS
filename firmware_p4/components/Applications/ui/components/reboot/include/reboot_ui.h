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

#ifndef REBOOT_UI_H
#define REBOOT_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load the full-screen "Rebooting..." screen.
 *
 * Must be called from the LVGL thread (or with the UI lock held).
 */
void reboot_ui_show(void);

/**
 * @brief Show the reboot screen and restart after a short hold.
 *
 * Loads the reboot screen, then restarts once it has had time to paint. The
 * restart runs the registered system shutdown handler, which unmounts storage.
 * Must be called from the LVGL thread.
 */
void reboot_ui_reboot(void);

#ifdef __cplusplus
}
#endif

#endif // REBOOT_UI_H
