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

#ifndef MESHTASTIC_PHONE_BRIDGE_H
#define MESHTASTIC_PHONE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Initialize the SPI-based Meshtastic phone bridge.
 *
 * Owns the transport plumbing between the local phoneapi state machine
 * (which lives on the P4) and the radios (BLE GATT and TCP server)
 * which terminate on the C5 co-processor.
 *
 * Registers a stream callback for SPI_ID_MESH_TORADIO_STREAM that
 * reassembles chunks back into StreamAPI frames and forwards them to
 * phoneapi_on_toradio.
 *
 * Spawns a notify task that periodically drains phoneapi_poll_fromradio,
 * fragments frames into SPI chunks, and ships them to the C5 via
 * SPI_ID_MESH_FROMRADIO_PUSH so they reach the connected app.
 *
 * Captures ESP_LOG output into a ring buffer and ships it to the C5 via
 * SPI_ID_MESH_LOG_PUSH whenever a phone has subscribed to LOGRADIO.
 *
 * Does not bring up BLE or WiFi by itself. Call meshtastic_phone_bridge_ble_start
 * and meshtastic_phone_bridge_wifi_start to enable each transport.
 *
 * @param node_num  Our node number, used for advertised name.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if already initialized
 *   - ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t meshtastic_phone_bridge_init(uint32_t node_num);

/**
 * @brief Tear down the bridge and stop the notify task.
 */
void meshtastic_phone_bridge_stop(void);

/**
 * @brief Ask the C5 to bring up the Meshtastic BLE GATT peripheral.
 *
 * @return ESP_OK on success, error code from the SPI bridge otherwise.
 */
esp_err_t meshtastic_phone_bridge_ble_start(void);

/**
 * @brief Ask the C5 to stop advertising and tear down the BLE GATT.
 *
 * @return ESP_OK on success, error code from the SPI bridge otherwise.
 */
esp_err_t meshtastic_phone_bridge_ble_stop(void);

/**
 * @brief Ask the C5 to start the Meshtastic TCP server (port 4403) and mDNS.
 *
 * @return ESP_OK on success, error code from the SPI bridge otherwise.
 */
esp_err_t meshtastic_phone_bridge_wifi_start(void);

/**
 * @brief Ask the C5 to stop the Meshtastic TCP server.
 *
 * @return ESP_OK on success, error code from the SPI bridge otherwise.
 */
esp_err_t meshtastic_phone_bridge_wifi_stop(void);

/**
 * @brief Whether at least one phone is currently connected (BLE or TCP).
 *
 * Result is cached for a short interval to avoid hammering the SPI bus.
 */
bool meshtastic_phone_bridge_is_connected(void);

/**
 * @brief Whether the phone app is actually paired and ready: BLE subscribed
 *        (after encryption) or a TCP client is attached — not just BLE-linked.
 *        Use this for the "linked" UI state.
 */
bool meshtastic_phone_bridge_is_subscribed(void);

#ifdef __cplusplus
}
#endif

#endif // MESHTASTIC_PHONE_BRIDGE_H
