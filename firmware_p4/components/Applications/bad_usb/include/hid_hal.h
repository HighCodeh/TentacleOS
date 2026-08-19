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

#ifndef HID_HAL_H
#define HID_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
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
 * @brief Callback type reporting whether the transport can accept a new report.
 *
 * Returns true once the previous report has been delivered to the host. The HAL
 * gates on this to send at the host's poll rate without overwriting a report the
 * host has not read yet (which would drop keys).
 */
typedef bool (*hid_ready_cb_t)(void);

/**
 * @brief Register the transport driver callbacks.
 *
 * This decouples the HAL from specific transports (USB, BLE, etc.).
 * Pass NULL for all parameters to unregister.
 *
 * @param send_cb   Keyboard report callback.
 * @param mouse_cb  Mouse report callback.
 * @param wait_cb   Connection wait callback.
 * @param ready_cb  Report-readiness callback (NULL falls back to fixed delays).
 */
void hid_hal_register_callback(hid_send_cb_t send_cb,
                               hid_mouse_cb_t mouse_cb,
                               hid_wait_cb_t wait_cb,
                               hid_ready_cb_t ready_cb);

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
