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

#ifdef __cplusplus
}
#endif

#endif // UI_HEADER_H
