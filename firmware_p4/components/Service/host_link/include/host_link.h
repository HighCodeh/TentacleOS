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

#ifndef HOST_LINK_H
#define HOST_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Host-link frame envelope (see docs/host_link/protocol.md):
//   [MAGIC 'H''B'][VER u8][FLAGS u8][COUNTER u32][LEN u16][BODY LEN][MAC 16 if FLAGS.auth]
// BODY = [type u8][category u8][op u8][payload...]
// Phase 1: no crypto — FLAGS.auth is 0 and no MAC is present/verified.

#define HOST_LINK_MAGIC0 0x48 // 'H'
#define HOST_LINK_MAGIC1 0x42 // 'B'
#define HOST_LINK_VER    1

#define HOST_LINK_HDR_SIZE 10 // MAGIC(2)+VER(1)+FLAGS(1)+COUNTER(4)+LEN(2)
#define HOST_LINK_MAC_SIZE 16

#define HOST_LINK_FLAG_AUTH 0x01

// BODY type (mirrors spi_type_t, plus LOG for pushed log text and the two
// pre-auth handshake frames). Handshake frames always travel unauthenticated
// (FLAGS.auth = 0, no envelope MAC); their proof is the mac_psk in the payload.
typedef enum {
  HOST_TYPE_CMD = 0x01,
  HOST_TYPE_RESP = 0x02,
  HOST_TYPE_STREAM = 0x03,
  HOST_TYPE_LOG = 0x04,
  HOST_TYPE_HELLO = 0x10, // app → device: { host_ver, client_nonce[16] }
  HOST_TYPE_HELLO_ACK =
      0x11, // device → app: { host_ver, server_nonce[16], device_id[6], mac_psk[16] }
} host_type_t;

#define HOST_LINK_NONCE_SIZE     16
#define HOST_LINK_DEVICE_ID_SIZE 6 // base MAC

// LOG frame: BODY = [type=LOG][category=0][op=0][source u8][level u8][utf-8 text].
// The two consoles in the app are split by `source`; coloring/filtering by `level`.
typedef enum {
  HOST_LOG_SRC_P4 = 0,
  HOST_LOG_SRC_C5 = 1,
} host_log_source_t;

typedef enum {
  HOST_LOG_LEVEL_ERROR = 0,
  HOST_LOG_LEVEL_WARN = 1,
  HOST_LOG_LEVEL_INFO = 2,
  HOST_LOG_LEVEL_DEBUG = 3,
  HOST_LOG_LEVEL_VERBOSE = 4,
} host_log_level_t;

// Transport write callback: emit one fully-framed host frame (CDC/BLE owns it).
typedef void (*host_link_writer_t)(const uint8_t *frame, size_t len);

/**
 * @brief Initialize the host-link layer (reassembly + dispatch state).
 */
esp_err_t host_link_init(void);

/**
 * @brief Claim the single companion session for a transport's writer.
 *
 * Only one transport (USB CDC or BLE) owns the session at a time; a second
 * transport's acquire is rejected while another holds it. The owning writer
 * receives all device→app frames (RESP/LOG/STREAM).
 *
 * @return true if the session is now owned by @p writer.
 */
bool host_link_session_acquire(host_link_writer_t writer);

/**
 * @brief Release the session if @p writer owns it; resets crypto + reassembly
 *        so a reconnecting app must re-handshake.
 */
void host_link_session_release(host_link_writer_t writer);

/** @brief True if @p writer currently owns the session. */
bool host_link_session_owns(host_link_writer_t writer);

/**
 * @brief Feed received transport bytes. May contain partial or multiple frames;
 *        complete frames are reassembled, dispatched, and answered via the
 *        registered writer.
 */
void host_link_feed(const uint8_t *data, size_t len);

/**
 * @brief Discard any partially-reassembled frame. Call when the transport drops
 *        so stale bytes don't bleed into the next session.
 */
void host_link_reset_rx(void);

/**
 * @brief True while a device → app frame is being written out. The log tee uses
 *        this to skip capturing logs emitted during a forward: that forward runs
 *        blocking SPI which can log (e.g. timeouts), and re-entering the tee on
 *        the forwarder's stack overflows it and amplifies (forwarded log → more
 *        SPI → more timeout logs).
 */
bool host_link_is_emitting(void);

/**
 * @brief Bring up the USB CDC-ACM companion transport: ensures the TinyUSB
 *        composite is installed, initializes the CDC interface, registers the
 *        CDC writer, and spawns the worker task that drains RX into the
 *        host-link core. Call after host_link_init().
 */
esp_err_t host_link_cdc_init(void);

/**
 * @brief Emit a single LOG frame to the app (device → app push).
 *
 * @param source    Which chip produced the line (P4 / C5).
 * @param level     Severity (maps to ESP_LOGx).
 * @param text      UTF-8 log text (ANSI already stripped by the caller).
 * @param text_len  Length of @p text in bytes (no NUL needed).
 */
void host_link_emit_log(host_log_source_t source,
                        host_log_level_t level,
                        const char *text,
                        size_t text_len);

/**
 * @brief Emit console-command output to the app (source=P4 LOG frames). Unlike
 *        host_link_emit_log this is NOT gated by the log-over-BLE toggle — it is
 *        the direct result of a command the app explicitly ran.
 */
void host_link_emit_console(const char *text, size_t text_len);

/**
 * @brief Push a STREAM frame to the app (device→app live data, e.g. sniffer
 *        records). @p category/@p op identify the originating operation.
 */
void host_link_emit_stream(uint8_t category, uint8_t op, const uint8_t *data, uint16_t len);

/**
 * @brief Tell the core which writer corresponds to the BLE transport, so the
 *        log-over-BLE toggle can gate background logs on BLE only.
 */
void host_link_mark_ble_writer(host_link_writer_t writer);

/**
 * @brief Install the P4 log tee: hooks esp_log_set_vprintf (preserving the local
 *        dev console), buffers lines in a drop-oldest ring, and spawns a worker
 *        that forwards them as LOG frames with source=P4. Call after
 *        host_link_cdc_init().
 */
esp_err_t host_link_log_init(void);

/**
 * @brief Register the C5 log relay: consumes the SPI_ID_SYSTEM_LOG stream from
 *        the C5 and re-emits each line as a LOG frame with source=C5. Call after
 *        the SPI bridge is up.
 */
esp_err_t host_link_c5log_init(void);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_H
