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

#include "buttons_gpio.h"

#include "input_manager.h"

// buttons_gpio is now a thin compatibility shim over input_manager, which owns
// the sampling, debounce, long-press/repeat, event queue, activity tracking and
// wake source. New code should consume input_manager events directly; these
// wrappers keep the existing call sites working unchanged. Button index order
// (0=UP..5=BACK) matches input_button_t, so it maps 1:1.

void buttons_init(void) {
  input_manager_init();
}

// Sampling now runs in the input_manager timer; there is nothing to poll here.
void buttons_task(void) {}

void buttons_sim_press(int idx, uint32_t ms) {
  if (idx >= 0 && idx < INPUT_BTN_COUNT) {
    input_sim_press((input_button_t)idx, ms);
  }
}

bool up_button_is_down(void) {
  return input_is_down(INPUT_BTN_UP);
}
bool down_button_is_down(void) {
  return input_is_down(INPUT_BTN_DOWN);
}
bool left_button_is_down(void) {
  return input_is_down(INPUT_BTN_LEFT);
}
bool right_button_is_down(void) {
  return input_is_down(INPUT_BTN_RIGHT);
}
bool ok_button_is_down(void) {
  return input_is_down(INPUT_BTN_OK);
}
bool back_button_is_down(void) {
  return input_is_down(INPUT_BTN_BACK);
}

bool up_button_pressed(void) {
  return input_consume_press(INPUT_BTN_UP);
}
bool down_button_pressed(void) {
  return input_consume_press(INPUT_BTN_DOWN);
}
bool left_button_pressed(void) {
  return input_consume_press(INPUT_BTN_LEFT);
}
bool right_button_pressed(void) {
  return input_consume_press(INPUT_BTN_RIGHT);
}
bool ok_button_pressed(void) {
  return input_consume_press(INPUT_BTN_OK);
}
bool back_button_pressed(void) {
  return input_consume_press(INPUT_BTN_BACK);
}
