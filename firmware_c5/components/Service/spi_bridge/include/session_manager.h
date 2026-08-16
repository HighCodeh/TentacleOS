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
 * @file session_manager.h
 * @brief Session lifecycle for long-running SPI bridge operations on C5.
 *
 * Each long-running op (sniffer, deauther, evil twin, etc.) opens a session
 * via session_manager_start(), emits streams via session_manager_try_emit(),
 * and receives heartbeat-driven keep-alive from the P4 master. If heartbeats
 * stop arriving for SESSION_TIMEOUT_MS, an internal watchdog task kills the
 * session and notifies the master via SPI_ID_SESSION_LOST stream.
 */

#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "spi_protocol.h"

/** Callback invoked by the watchdog when a session is auto-killed. */
typedef void (*session_kill_cb_t)(spi_id_t op_id);

/**
 * @brief Initialize the session manager and start its watchdog task.
 *
 * Must be called once at boot before any operation handlers run.
 */
void session_manager_init(void);

/**
 * @brief Open a new session for a long-running operation.
 *
 * Closes any previous active session (calling its kill callback) before
 * opening the new one. Only one session is active at a time.
 *
 * @param op_id    SPI command ID identifying the operation.
 * @param kill_cb  Callback invoked if the watchdog later kills this session.
 * @return         New session_id (>0), or SPI_SESSION_INVALID_ID on error.
 */
uint32_t session_manager_start(spi_id_t op_id, session_kill_cb_t kill_cb);

/**
 * @brief Close a session explicitly (called from STOP handlers).
 *
 * @param session_id  Session to close.
 * @return            ESP_OK if matched and closed, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t session_manager_stop(uint32_t session_id);

/**
 * @brief Refresh the watchdog and update last-acked sequence.
 *
 * Called by the bridge on receipt of SPI_ID_SESSION_HEARTBEAT.
 *
 * @param session_id      Session being heartbeated.
 * @param last_acked_seq  Highest stream seq the master has processed.
 * @return                true if heartbeat matched the active session.
 */
bool session_manager_heartbeat(uint32_t session_id, uint32_t last_acked_seq);

/**
 * @brief Attempt to emit a stream packet for the active session.
 *
 * Prefixes spi_stream_meta_t and emits via SPI_TYPE_STREAM. Drops the packet
 * silently (with internal counter) if the master's ack window is full.
 *
 * @param session_id  Session emitting (must match the active one).
 * @param data        Operation-specific payload.
 * @param len         Payload length (must fit SPI_MAX_PAYLOAD - sizeof(meta)).
 * @return            ESP_OK if emitted, ESP_ERR_INVALID_STATE if session
 *                    is not active, ESP_ERR_NO_MEM if backpressure dropped it.
 */
esp_err_t session_manager_try_emit(uint32_t session_id, const uint8_t *data, uint8_t len);

/**
 * @brief True while a long-running app session is open (sniffer/spam/flood/etc.).
 *
 * Used by the BT arbiter to avoid tearing NimBLE out from under an active app.
 */
bool session_manager_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // SESSION_MANAGER_H
