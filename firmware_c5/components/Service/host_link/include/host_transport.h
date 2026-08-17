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

#ifndef HOST_TRANSPORT_H
#define HOST_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "spi_protocol.h"

// Companion host-link transport on the C5: chunk/reassemble opaque host frames
// between BLE (host_link_gatt) and the SPI bridge to the P4. The C5 never parses
// payloads — it only moves bytes. Mirrors meshcore_transport.

/** @brief Init the transport (mutex, reassembly state) and enable the RX stream. */
esp_err_t host_transport_init(void);

/**
 * @brief Feed one P4→C5 chunk (from SPI_ID_HOST_TX). Reassembles per seq and,
 *        on the last chunk, notifies the reassembled frame over BLE.
 */
void host_transport_inject_tx_chunk(const uint8_t *payload, uint8_t len);

/**
 * @brief Chunk a BLE-received frame and push it to the P4 over the
 *        SPI_ID_HOST_RX stream. Called from the GATT write handler.
 */
bool host_transport_send_to_p4(const uint8_t *frame, uint16_t len);

/** @brief Fill the BLE connection status for SPI_ID_HOST_STATUS. */
void host_transport_get_status(spi_host_status_t *out_status);

/** @brief Clear reassembly + sequence state (on disconnect / stop). */
void host_transport_reset(void);

#ifdef __cplusplus
}
#endif

#endif // HOST_TRANSPORT_H
