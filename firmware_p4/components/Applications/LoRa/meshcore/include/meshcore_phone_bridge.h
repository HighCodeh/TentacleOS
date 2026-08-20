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

#ifndef MESHCORE_PHONE_BRIDGE_H
#define MESHCORE_PHONE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Initialize the SPI-based MeshCore phone bridge.
 *
 * Owns the transport plumbing between the local phoneapi protocol layer
 * (which lives on the P4) and the BLE NUS service which terminates on
 * the C5 co-processor.
 *
 * Registers a stream callback for SPI_ID_MCORE_RX_STREAM that reassembles
 * chunks back into Companion command frames and forwards them to
 * meshcore_phoneapi_on_inbound. Hooks meshcore_phoneapi outbound so any
 * frame produced on the P4 (responses, pushes, sync drains) is fragmented
 * into SPI chunks and shipped to the C5 via SPI_ID_MCORE_TX_PUSH.
 *
 * Spawns a status task that polls SPI_ID_MCORE_STATUS so the bridge can
 * react to phone (dis)connect events and retry MCORE_BLE_INIT if the C5
 * was rebooted.
 *
 * Does not bring up BLE by itself. Call meshcore_phone_bridge_ble_start
 * after the protocol layer is ready.
 *
 * @param name_prefix  Advertised name prefix on the C5 ("<prefix>-XXXX",
 *                     where XXXX is the last 2 octets of the MAC). May
 *                     be NULL to fall back to "Highboy-MC".
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if already initialized
 *   - ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t meshcore_phone_bridge_init(const char *name_prefix);

/**
 * @brief Tear down the bridge and stop the status task.
 */
void meshcore_phone_bridge_stop(void);

/**
 * @brief Ask the C5 to bring up the MeshCore BLE NUS peripheral.
 *
 * @return ESP_OK on success, error code from the SPI bridge otherwise.
 */
esp_err_t meshcore_phone_bridge_ble_start(void);

/**
 * @brief Ask the C5 to stop advertising and tear down the BLE stack.
 *
 * @return ESP_OK on success, error code from the SPI bridge otherwise.
 */
esp_err_t meshcore_phone_bridge_ble_stop(void);

/**
 * @brief Whether a phone is currently connected to the BLE NUS service.
 *
 * Result is cached for a short interval to avoid hammering the SPI bus.
 */
bool meshcore_phone_bridge_is_connected(void);

/**
 * @brief Whether the connected phone has subscribed to notifications, i.e. the
 *        companion app is actually paired and ready (not just BLE-linked).
 *
 * Use this for the "linked" UI state: ble_connected goes true at the raw BLE
 * link (before the pairing PIN even shows), while ble_subscribed only goes true
 * after encryption + the app subscribing.
 */
bool meshcore_phone_bridge_is_subscribed(void);

#ifdef __cplusplus
}
#endif

#endif // MESHCORE_PHONE_BRIDGE_H
