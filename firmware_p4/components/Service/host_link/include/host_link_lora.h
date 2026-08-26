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

#ifndef HOST_LINK_LORA_H
#define HOST_LINK_LORA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// P4-local LoRa companion ops: radio config (category 0x0B) and broadcast chat
// (category 0x0C). The SX1262 is on the P4, so these run locally and are never
// relayed to the C5. Chat is backed by the MeshCore stack via the lora_session
// facade; received messages are pushed to the app as LORACHAT.RX STREAM frames.

/**
 * @brief Execute a LoRa config/chat companion op locally.
 *
 * @param cmd      Packed command id (SPI_ID_LORACFG_* / SPI_ID_LORACHAT_*).
 * @param payload  Request payload (layout per op; see host_link_lora.c).
 * @param plen     Payload length.
 * @param out_data Response data buffer (written without the status byte).
 * @param out_cap  Capacity of @p out_data.
 * @param out_len  Receives the response data length.
 * @return spi_status_t value.
 */
uint8_t host_lora_handle(uint16_t cmd,
                         const uint8_t *payload,
                         uint16_t plen,
                         uint8_t *out_data,
                         uint16_t out_cap,
                         uint16_t *out_len);

/**
 * @brief Stop the chat RX relay task (leaves the on-device radio running).
 *
 * Called when the host-link session is released so the companion stops emitting
 * LORACHAT.RX streams once the app disconnects.
 */
void host_lora_stop(void);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_LORA_H
