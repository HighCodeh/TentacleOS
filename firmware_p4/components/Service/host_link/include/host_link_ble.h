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

#ifndef HOST_LINK_BLE_H
#define HOST_LINK_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"

// BLE companion transport for the host link. BLE terminates on the C5; this
// module relays opaque host frames over the SPI bridge (SPI_ID_HOST_RX stream
// in, SPI_ID_HOST_TX push out) and drives the C5 GATT server. The P4 owns the
// security envelope — the C5 only moves bytes. Mirrors the MeshCore phone
// bridge.

/**
 * @brief Set up the BLE relay (stream callback + status task). Does NOT start
 *        advertising; call host_link_ble_start() for that. Run after
 *        host_link_init().
 */
esp_err_t host_link_ble_init(void);

/** @brief Ask the C5 to start the GATT server and advertise. */
esp_err_t host_link_ble_start(void);

/** @brief Ask the C5 to stop the GATT server. */
esp_err_t host_link_ble_stop(void);

/** @brief True if a companion is connected over BLE. */
bool host_link_ble_is_connected(void);

/** @brief True if the companion BLE advertising is currently requested on. */
bool host_link_ble_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_BLE_H
