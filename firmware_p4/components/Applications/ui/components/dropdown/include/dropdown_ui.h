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

#ifndef UI_DROPDOWN_H
#define UI_DROPDOWN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/** @brief Create the dropdown panel on the given parent. */
void dropdown_ui_create(lv_obj_t *parent);

/**
 * @brief Create the single global dropdown on the top layer (call once at init).
 *
 * The panel then rides above every screen and is opened from any browse screen
 * with a long-press of UP (gated by ui_screen_shows_chrome), closed with BACK or
 * another long-press. While open it holds exclusive input (keypad suppressed +
 * polling screens input-locked).
 */
void dropdown_ui_global_init(void);

/** @brief Register objects to hide when the dropdown opens. */
void dropdown_ui_register_hide_objs(lv_obj_t **objs, int count);

/** @brief Check if the dropdown is currently open. */
bool dropdown_ui_is_open(void);

/** @brief Bring the dropdown to the foreground. */
void dropdown_ui_raise(void);

#ifdef __cplusplus
}
#endif

#endif // UI_DROPDOWN_H
