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

#ifndef BAD_BLE_H
#define BAD_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Initialize the BadBLE transport for DuckyScript.
 *
 * Acquires the BLE host resource, starts the HID-over-GATT keyboard on the C5
 * (advertising), and registers the BLE keyboard callback into the HID HAL so the
 * transport-agnostic DuckyScript engine drives keys over Bluetooth. Mirror of
 * bad_usb_init() for the BLE transport.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if already initialized or the BLE host is busy
 *   - the underlying error if ble_hid_init() fails
 */
esp_err_t bad_ble_init(void);

/**
 * @brief Deinitialize the BadBLE transport.
 *
 * Unregisters the HID callbacks, stops the C5 HID keyboard, and releases the BLE
 * host resource.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t bad_ble_deinit(void);

/**
 * @brief Abort predicate polled while waiting for a BLE host connection.
 *
 * @return true to cancel the wait, false to keep waiting.
 */
typedef bool (*bad_ble_abort_cb_t)(void);

/**
 * @brief Block until a Bluetooth host pairs and connects, or a timeout elapses.
 */
void bad_ble_wait_for_connection(void);

/**
 * @brief Abortable variant of bad_ble_wait_for_connection().
 *
 * Polls ble_hid_is_connected() while calling the abort predicate between polls,
 * so a caller can cancel a run that is still waiting for a host that never pairs.
 *
 * @param should_abort  Predicate polled between waits; NULL waits until timeout.
 * @return true if a host connected, false on timeout or abort.
 */
bool bad_ble_wait_for_connection_ex(bad_ble_abort_cb_t should_abort);

#ifdef __cplusplus
}
#endif

#endif // BAD_BLE_H
