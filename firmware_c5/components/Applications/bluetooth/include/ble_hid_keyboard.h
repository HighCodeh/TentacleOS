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

#ifndef BLE_HID_KEYBOARD_H
#define BLE_HID_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Initialize the HID over GATT (HOGP) service and start advertising as a keyboard.
 *
 * @return
 *   - ESP_OK on success
 */
esp_err_t ble_hid_init(void);

/** Register the composite HID service before the NimBLE host starts. */
esp_err_t ble_hid_register_services(void);

/** Return whether the HID service belongs to the current NimBLE instance. */
bool ble_hid_services_registered(void);

/** Clear HID runtime state after the shared Bluetooth stack is deinitialized. */
void ble_hid_stack_reset(void);

/**
 * @brief Stop the HID service and disconnect.
 *
 * @return
 *   - ESP_OK on success
 */
esp_err_t ble_hid_deinit(void);

/**
 * @brief Check if a host is connected and ready to receive keystrokes.
 *
 * @return true if connected, false otherwise.
 */
bool ble_hid_is_connected(void);

/**
 * @brief Send a key press via Bluetooth HID.
 *
 * @param keycode  HID keycode (e.g., 0x04 for 'A').
 * @param modifier Modifier flags (Shift, Ctrl, Alt, GUI).
 */
void ble_hid_send_key(uint8_t keycode, uint8_t modifier);

/** Send a relative mouse input report through the composite HID service. */
void ble_hid_send_mouse(int8_t x, int8_t y, uint8_t buttons, int8_t wheel);

#ifdef __cplusplus
}
#endif

#endif // BLE_HID_KEYBOARD_H
