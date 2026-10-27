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

#ifndef HOST_LINK_BADUSB_H
#define HOST_LINK_BADUSB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// P4-local BadUSB companion ops (category 0x0A). The USB-HID peripheral is on
// the P4, so these run locally and are never relayed to the C5. RUN spawns a
// worker task and returns immediately; poll STATUS for completion.

/**
 * @brief Execute a BadUSB companion op locally.
 *
 * RUN (payload = script path) starts a DuckyScript run; ABORT stops it; STATUS
 * returns one byte (1 = a run is active, 0 = idle).
 *
 * @param cmd      Packed command id (SPI_ID_BADUSB_*).
 * @param payload  Request payload.
 * @param plen     Payload length.
 * @param out_data Response data buffer (written without the status byte).
 * @param out_cap  Capacity of @p out_data.
 * @param out_len  Receives the response data length.
 * @return spi_status_t value.
 */
uint8_t host_badusb_handle(uint16_t cmd,
                           const uint8_t *payload,
                           uint16_t plen,
                           uint8_t *out_data,
                           uint16_t out_cap,
                           uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_BADUSB_H
