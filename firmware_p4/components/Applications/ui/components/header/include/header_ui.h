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

#ifndef UI_HEADER_H
#define UI_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "lvgl.h"

/** @brief Create the header bar on the given parent. */
void header_ui_create(lv_obj_t *parent);

/**
 * @brief Tell the header whether BLE is currently active/connected.
 *        Call from the BLE service when the link state changes; the
 *        header will tint the BT icon green when true. Default is false
 *        (icon stays in its default white tone).
 */
void header_ui_set_ble_active(bool active);

/**
 * @brief Report the cached SD card usage, computed off the LVGL task.
 *
 * Reads a value cached by the header's mount worker, so callers (e.g. the
 * home dropdown) avoid a blocking filesystem query on the LVGL task.
 *
 * @param[out] out_used_pct  Used space percentage (0-100); may be NULL.
 * @return true if a card is currently mounted/recognized.
 */
bool header_ui_sd_usage(int *out_used_pct);

#ifdef __cplusplus
}
#endif

#endif // UI_HEADER_H
