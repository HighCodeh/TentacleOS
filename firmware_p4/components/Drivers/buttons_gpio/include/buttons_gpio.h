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

#ifndef BUTTONS_GPIO_H
#define BUTTONS_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "pin_def.h"

/** @brief Debounce/edge state tracked for a single GPIO push-button. */
typedef struct {
  uint32_t gpio;     ///< GPIO number the button is wired to.
  bool last_state;   ///< Last raw level sampled by buttons_task().
  bool pressed_flag; ///< Latched "fresh press" flag, drained by the read paths.
} button_t;

/**
 * @brief Initialize GPIO buttons.
 */
void buttons_init(void);

/**
 * @brief Poll button states. Call periodically from a task.
 */
void buttons_task(void);

/**
 * @brief Check if the up button was pressed since last check.
 *
 * @return true if pressed, false otherwise.
 */
bool up_button_pressed(void);

/**
 * @brief Check if the down button was pressed since last check.
 *
 * @return true if pressed, false otherwise.
 */
bool down_button_pressed(void);

/**
 * @brief Check if the left button was pressed since last check.
 *
 * @return true if pressed, false otherwise.
 */
bool left_button_pressed(void);

/**
 * @brief Check if the right button was pressed since last check.
 *
 * @return true if pressed, false otherwise.
 */
bool right_button_pressed(void);

/**
 * @brief Check if the ok button was pressed since last check.
 *
 * @return true if pressed, false otherwise.
 */
bool ok_button_pressed(void);

/**
 * @brief Check if the back button was pressed since last check.
 *
 * @return true if pressed, false otherwise.
 */
bool back_button_pressed(void);

/**
 * @brief Check if the up button is currently held down.
 *
 * @return true if held, false otherwise.
 */
bool up_button_is_down(void);

/**
 * @brief Check if the down button is currently held down.
 *
 * @return true if held, false otherwise.
 */
bool down_button_is_down(void);

/**
 * @brief Check if the left button is currently held down.
 *
 * @return true if held, false otherwise.
 */
bool left_button_is_down(void);

/**
 * @brief Check if the right button is currently held down.
 *
 * @return true if held, false otherwise.
 */
bool right_button_is_down(void);

/**
 * @brief Check if the ok button is currently held down.
 *
 * @return true if held, false otherwise.
 */
bool ok_button_is_down(void);

/**
 * @brief Check if the back button is currently held down.
 *
 * @return true if held, false otherwise.
 */
bool back_button_is_down(void);

/**
 * @brief Inject a simulated button press for @p ms milliseconds.
 *
 * Makes the button at @p idx read as "down" until the deadline so the LVGL
 * keypad indev and screen nav timers observe an injected press. Index order:
 * 0=UP, 1=DOWN, 2=LEFT, 3=RIGHT, 4=OK, 5=BACK. Used by the console `key`
 * command for headless UI navigation and screen capture.
 *
 * @param idx  Button index (0-5).
 * @param ms   Duration to hold the simulated press, in milliseconds.
 */
void buttons_sim_press(int idx, uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif // BUTTONS_GPIO_H
