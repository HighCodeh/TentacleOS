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

#ifndef KEYBOARD_UI_H
#define KEYBOARD_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "lvgl.h"

/**
 * @brief Callback invoked when the on-screen keyboard submits its text.
 *
 * @param text       The submitted text. Valid only during the callback scope.
 * @param user_data  User context passed to keyboard_open().
 */
typedef void (*keyboard_submit_cb_t)(const char *text, void *user_data);

/** @brief Open the on-screen keyboard. */
void keyboard_open(lv_obj_t *target_textarea, keyboard_submit_cb_t cb, void *user_data);

/** @brief Close the on-screen keyboard. */
void keyboard_close(void);

/** @brief Whether the on-screen keyboard overlay is currently shown. */
bool keyboard_is_open(void);

#ifdef __cplusplus
}
#endif

#endif // KEYBOARD_UI_H
