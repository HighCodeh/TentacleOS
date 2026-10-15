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

/**
 * @file spi_session.h
 * @brief Master-side session lifecycle for long-running C5 operations.
 *
 * Wraps the SPI bridge with session-based reliability:
 *   - START handshake returns a session_id from the slave.
 *   - Per-session heartbeat task pings the slave every 2s; if the slave
 *     stops acknowledging, the local on_lost callback fires.
 *   - Stream packets carry session_id + seq; the wrapper drops stale
 *     packets and counts received seq for backpressure ack.
 *
 * Migration from raw bridge calls:
 *   spi_bridge_send_command(SPI_ID_WIFI_APP_X, params, ...)
 *   + spi_bridge_register_stream_cb(SPI_ID_WIFI_APP_X, cb)
 * becomes:
 *   spi_session_start(SPI_ID_WIFI_APP_X, params, ..., cb, on_lost)
 *
 * See spi_bridge/README.md "Session Lifecycle" for the full design.
 */

#ifndef SPI_SESSION_H
#define SPI_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "spi_protocol.h"

/** Stream payload callback (meta header is stripped before invocation). */
typedef void (*spi_session_stream_cb_t)(const uint8_t *data, uint8_t len);

/**
 * @brief Source session id of the current thread's stream callback.
 *
 * Returns
 * SPI_SESSION_INVALID_ID outside a callback. The value remains the
 * original packet's id even
 * when another task stops/replaces the session after
 * metadata validation. Consumers that defer
 * work must copy it with their data;
 * it must not be replaced by the id of a newly started
 * session.
 */
uint32_t spi_session_callback_id(void);

/** Invoked when the C5 stops acknowledging this session (heartbeat fail or LOST stream). */
typedef void (*spi_session_lost_cb_t)(uint32_t session_id, spi_id_t op_id);

/**
 * @brief Initialize the session client. Must be called once at boot.
 *
 * Registers the SPI_ID_SESSION_LOST stream listener.
 */
void spi_session_init(void);

/**
 * @brief Start a long-running operation with full session lifecycle.
 *
 * Sends @p op_id with @p params as command payload, expects an
 * spi_session_resp_t reply, then spawns a heartbeat task and registers
 * a stream listener at @p op_id that strips meta and forwards to
 * @p on_stream.
 *
 * @param op_id      SPI command identifier of the operation.
 * @param params     Optional command params (may be NULL).
 * @param params_len Length of @p params (0 if NULL).
 * @param on_stream  Callback for stream payloads (called from bridge task).
 * @param on_lost    Callback if session dies (heartbeat timeout / LOST stream).
 * @return           Session id on success, SPI_SESSION_INVALID_ID on failure.
 */
uint32_t spi_session_start(spi_id_t op_id,
                           const uint8_t *params,
                           uint8_t params_len,
                           spi_session_stream_cb_t on_stream,
                           spi_session_lost_cb_t on_lost);

/**
 * @brief Cleanly stop an active session.
 *
 * Sends a STOP command (with @p op_id) carrying the session_id, kills
 * the heartbeat task and unregisters the stream listener.
 *
 * @param session_id  Session to stop.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if session doesn't match.
 */
esp_err_t spi_session_stop(uint32_t session_id);

/**
 * @brief Check whether a given session is currently active locally.
 */
bool spi_session_is_active(uint32_t session_id);

#ifdef __cplusplus
}
#endif

#endif // SPI_SESSION_H
