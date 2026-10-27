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

#ifndef HOST_LINK_IR_H
#define HOST_LINK_IR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Handle an IR host-link command locally on the P4 (driving Service/ir).
 *
 * TX and torch are synchronous. RX is streamed: RX_START spawns a capture task
 * that pushes each decoded signal as a STREAM frame (SPI_ID_IR_RX_FRAME); RX_STOP
 * tears it down.
 *
 * @return spi_status_t.
 */
uint8_t host_ir_handle(uint16_t cmd,
                       const uint8_t *payload,
                       uint16_t plen,
                       uint8_t *out,
                       uint16_t out_cap,
                       uint16_t *out_len);

/** @brief Stop the RX capture task if running. Safe to call when idle. */
void host_ir_stop_rx(void);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_IR_H
