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

#ifndef BOOT_MAP_UI_H
#define BOOT_MAP_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the boot-map viewer (BACK returns to the developer menu).
 *
 * Lists each kernel_init subsystem from boot_report with its OK/FAIL/skip state.
 */
void ui_boot_map_open(void);

/**
 * @brief Open the boot-map viewer with a custom BACK action.
 *
 * Used by safe mode so BACK returns to the recovery menu instead of the normal
 * developer menu. Call under the LVGL lock.
 */
void ui_boot_map_open_cb(void (*on_back)(void));

#ifdef __cplusplus
}
#endif

#endif // BOOT_MAP_UI_H
