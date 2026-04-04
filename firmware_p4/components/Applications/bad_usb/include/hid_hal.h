// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HID_HAL_H
#define HID_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Callback type for sending a keyboard HID report.
 *
 * @param keycode    HID keycode to press.
 * @param modifiers  Modifier bitmask (CTRL, SHIFT, ALT, GUI).
 */
typedef void (*hid_send_cb_t)(uint8_t keycode, uint8_t modifiers);

/**
 * @brief Callback type for sending a mouse HID report.
 *
 * @param x        Relative X movement (-127 to 127).
 * @param y        Relative Y movement (-127 to 127).
 * @param buttons  Button bitmask.
 * @param wheel    Scroll wheel delta.
 */
typedef void (*hid_mouse_cb_t)(int8_t x, int8_t y, uint8_t buttons, int8_t wheel);

/**
 * @brief Callback type for waiting until the HID transport is connected.
 */
typedef void (*hid_wait_cb_t)(void);

/**
 * @brief Register the transport driver callbacks.
 *
 * This decouples the HAL from specific transports (USB, BLE, etc.).
 * Pass NULL for all parameters to unregister.
 *
 * @param send_cb   Keyboard report callback.
 * @param mouse_cb  Mouse report callback.
 * @param wait_cb   Connection wait callback.
 */
void hid_hal_register_callback(hid_send_cb_t send_cb,
                               hid_mouse_cb_t mouse_cb,
                               hid_wait_cb_t wait_cb);

/**
 * @brief Press and release a key.
 *
 * Sends a key-down report followed by a key-up report with ~5 ms
 * delay between each to satisfy USB polling intervals.
 *
 * @param keycode    HID keycode.
 * @param modifiers  Modifier bitmask.
 */
void hid_hal_press_key(uint8_t keycode, uint8_t modifiers);

/**
 * @brief Move the mouse cursor.
 *
 * @param x  Relative X movement (-127 to 127).
 * @param y  Relative Y movement (-127 to 127).
 */
void hid_hal_mouse_move(int8_t x, int8_t y);

/**
 * @brief Click a mouse button (press + release).
 *
 * @param buttons  Button bitmask (MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, etc.).
 */
void hid_hal_mouse_click(uint8_t buttons);

/**
 * @brief Scroll the mouse wheel.
 *
 * @param wheel  Scroll delta (positive = up, negative = down).
 */
void hid_hal_mouse_scroll(int8_t wheel);

/**
 * @brief Block until the transport reports a successful connection.
 */
void hid_hal_wait_for_connection(void);

#ifdef __cplusplus
}
#endif

#endif // HID_HAL_H
