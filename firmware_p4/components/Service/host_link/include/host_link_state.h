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

#ifndef HOST_LINK_STATE_H
#define HOST_LINK_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Companion device state, settings (toggles), and raw console execution. All
// P4-local host-link ops (never relayed to the C5).

/** @brief Load the toggle settings from NVS (defaults to on). */
esp_err_t host_link_state_init(void);

/** @brief True if the app may run raw console lines (default on). */
bool host_settings_console_exec_enabled(void);

/** @brief True if logs may be delivered over BLE (default on; USB always on). */
bool host_settings_log_over_ble_enabled(void);

/**
 * @brief Execute a device-state / settings / console-exec op locally.
 *
 * @param cmd      Packed command id (SPI_ID_SYSTEM_DEVICE_STATE / *_SETTINGS / *_CONSOLE_EXEC).
 * @param payload  Request payload.
 * @param plen     Payload length.
 * @param out_data Response data buffer (written without the status byte).
 * @param out_cap  Capacity of @p out_data.
 * @param out_len  Receives the response data length.
 * @return spi_status_t value.
 */
uint8_t host_state_handle(uint16_t cmd,
                          const uint8_t *payload,
                          uint16_t plen,
                          uint8_t *out_data,
                          uint16_t out_cap,
                          uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_STATE_H
