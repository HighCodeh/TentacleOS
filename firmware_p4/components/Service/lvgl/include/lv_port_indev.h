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

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "lvgl.h"

/** Keypad input device (physical buttons mapped to LVGL keys). */
extern lv_indev_t *indev_keypad;

/** Default navigation group. Widgets are auto-added to this group. */
extern lv_group_t *main_group;

/**
 * @brief Initialize the LVGL input device driver.
 *
 * Creates a KEYPAD input device, a default navigation group, and
 * binds them together. Physical buttons are polled via the
 * buttons_gpio driver.
 */
void lv_port_indev_init(void);

/**
 * @brief Toggle keyboard navigation mode.
 *
 * When enabled, UP/DOWN buttons send LV_KEY_UP/LV_KEY_DOWN instead
 * of LV_KEY_PREV/LV_KEY_NEXT. Useful for text input or list scrolling.
 *
 * @param is_enabled  true for keyboard mode, false for navigation mode.
 */
void lv_port_indev_set_keyboard_mode(bool is_enabled);

/**
 * @brief Inject a remote key press (companion app screen-share control).
 *
 * Plays out one PRESS + one RELEASE of the given LVGL key on the keypad indev,
 * as if a physical button were tapped. Physical buttons take priority.
 *
 * @param lv_key  LVGL key code (LV_KEY_UP/DOWN/LEFT/RIGHT/ENTER/ESC).
 */
void lv_port_indev_inject(uint32_t lv_key);

#ifdef __cplusplus
}
#endif

#endif // LV_PORT_INDEV_H
