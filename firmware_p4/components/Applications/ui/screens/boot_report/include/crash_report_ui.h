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

#ifndef CRASH_REPORT_UI_H
#define CRASH_REPORT_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the last-crash viewer (BACK returns to the developer menu).
 *
 * Shows the previous run's reset reason and, if a core dump was captured, the
 * faulting task and RISC-V fault registers. OK clears the stored dump.
 */
void ui_crash_report_open(void);

/**
 * @brief Open the last-crash viewer with a custom BACK action.
 *
 * Used by safe mode so BACK returns to the recovery menu. Call under the LVGL
 * lock.
 */
void ui_crash_report_open_cb(void (*on_back)(void));

#ifdef __cplusplus
}
#endif

#endif // CRASH_REPORT_UI_H
