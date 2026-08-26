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

#ifndef HOST_LINK_STREAM_H
#define HOST_LINK_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Companion streaming + heartbeat proxy. Long-running ops the app starts (e.g.
// the WiFi sniffer) run through the existing spi_session model; their records
// are pushed to the app as STREAM frames. The app heartbeats the P4 to prove
// liveness; if it goes silent (or the link drops) the P4 tears the session
// down, which stops its own heartbeat to the C5 → the C5 watchdog kills it.

/** @brief Init the streaming proxy (mutex + app-liveness watchdog state). */
esp_err_t host_link_stream_init(void);

/**
 * @brief Start a session-based stream on the app's behalf (spi_session_start)
 *        and begin pushing its records as STREAM frames.
 *
 * @return spi_status_t; on OK writes the session id (u32) into @p out_data.
 */
uint8_t host_stream_start(uint16_t cmd,
                          const uint8_t *payload,
                          uint16_t plen,
                          uint8_t *out_data,
                          uint16_t out_cap,
                          uint16_t *out_len);

/**
 * @brief Handle a SESSION-category control command from the app (HEARTBEAT /
 *        STOP). Heartbeats refresh app liveness; STOP tears the session down.
 *        These are P4-local — the P4 keeps heartbeating the C5 itself.
 */
uint8_t host_stream_session_ctrl(uint16_t cmd,
                                 const uint8_t *payload,
                                 uint16_t plen,
                                 uint8_t *out_data,
                                 uint16_t out_cap,
                                 uint16_t *out_len);

/** @brief Tear down any active stream (called on companion link loss). */
void host_stream_teardown(void);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_STREAM_H
