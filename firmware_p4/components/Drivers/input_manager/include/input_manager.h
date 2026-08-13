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

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Central button input for the P4. One periodic sampler (esp_timer) debounces
// all six buttons, drives a per-button state machine (press / release /
// long-press / auto-repeat), pushes events onto a queue, tracks the last
// interaction for the power policy, and registers the button GPIOs as a wake
// source. Screens consume events instead of polling raw GPIO; the legacy
// `buttons_gpio` API is a thin shim over this module.

typedef enum {
  INPUT_BTN_UP = 0,
  INPUT_BTN_DOWN,
  INPUT_BTN_LEFT,
  INPUT_BTN_RIGHT,
  INPUT_BTN_OK,
  INPUT_BTN_BACK,
  INPUT_BTN_COUNT,
} input_button_t;

typedef enum {
  INPUT_ACTION_PRESS = 0,  ///< debounced edge: button went down
  INPUT_ACTION_RELEASE,    ///< debounced edge: button came up
  INPUT_ACTION_LONG_PRESS, ///< held past the long-press threshold (fires once)
  INPUT_ACTION_REPEAT,     ///< auto-repeat while held (after the initial delay)
} input_action_t;

typedef struct {
  input_button_t button;
  input_action_t action;
  uint32_t timestamp_ms; ///< ms since boot when the event was produced
} input_event_t;

/**
 * @brief Start the input manager: configure the button GPIOs, arm the sampling
 *        timer, create the event queue, and register the wake source.
 *
 * Idempotent: a second call is a no-op. Safe to call from early boot.
 */
esp_err_t input_manager_init(void);

/**
 * @brief Pop the next input event.
 *
 * @param out_event    Filled with the event on success.
 * @param timeout_ms   How long to block waiting for an event (0 = poll).
 * @return true if an event was dequeued, false on timeout.
 */
bool input_get_event(input_event_t *out_event, uint32_t timeout_ms);

/**
 * @brief Current debounced held state of a button.
 */
bool input_is_down(input_button_t button);

/**
 * @brief Consume the latched "fresh press" for a button (edge, single-shot).
 *
 * Backs the legacy `*_button_pressed()` shim; migrated screens should use the
 * event queue instead.
 */
bool input_consume_press(input_button_t button);

/**
 * @brief Timestamp (ms since boot) of the last button activity.
 *
 * The power policy computes idle time as `now_ms - input_last_activity_ms()`.
 */
uint32_t input_last_activity_ms(void);

/**
 * @brief Simulate a press for @p ms so the sampler reads the button as held.
 *
 * Used by the console `key` command for headless navigation and capture. The
 * simulated press runs through the same debounce/state machine, so it also
 * produces real events.
 */
void input_sim_press(input_button_t button, uint32_t ms);

/**
 * @brief Register the button GPIOs as a light-sleep wake source.
 *
 * Called by @ref input_manager_init; exposed so the power policy can re-arm it
 * after reconfiguring sleep.
 */
esp_err_t input_configure_wake_source(void);

#ifdef __cplusplus
}
#endif

#endif // INPUT_MANAGER_H
