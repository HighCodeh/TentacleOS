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
// HELLO/ACK are unauthenticated. All other frames require a fresh keyed MAC.

#define HOST_LINK_MAGIC0 0x48 // 'H'
#define HOST_LINK_MAGIC1 0x42 // 'B'
#define HOST_LINK_VER    1

#define HOST_LINK_HDR_SIZE 10 // MAGIC(2)+VER(1)+FLAGS(1)+COUNTER(4)+LEN(2)
#define HOST_LINK_MAC_SIZE 16

#define HOST_LINK_FLAG_AUTH 0x01

// BODY type (mirrors spi_type_t, plus LOG for pushed log text and the initial
// handshake frames). HELLO and HELLO_ACK travel unauthenticated
// (FLAGS.auth = 0, no envelope MAC); their proof is the mac_psk in the payload.
typedef enum {
  HOST_TYPE_CMD = 0x01,
  HOST_TYPE_RESP = 0x02,
  HOST_TYPE_STREAM = 0x03,
  HOST_TYPE_LOG = 0x04,
  HOST_TYPE_HELLO = 0x10,     // app → device: v2 nonce, ephemeral public key, PSK proof
  HOST_TYPE_HELLO_ACK = 0x11, // device → app: v2 transcript-bound proof
  HOST_TYPE_HELLO_FINISH = 0x12,
  HOST_TYPE_HELLO_READY = 0x13,
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
typedef void (*host_link_writer_t)(uint32_t generation, const uint8_t *frame, size_t len);

/**
 * @brief Initialize the host-link layer (reassembly + dispatch state).
 */
esp_err_t host_link_init(void);
// Start ingress resources from a transport INIT task, never from a callback.
esp_err_t host_link_start_worker(void);

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
 * @brief Callback-safe invalidation if @p writer owns it. Crypto, reassembly
 *        and producer
 * teardown are deferred to the core worker. Never waits
 *        for a handler, TX, crypto or SPI
 * operation.
 */
void host_link_session_release(host_link_writer_t writer);
// Fail a specific transfer only, without revoking a newer reuse of the writer.
void host_link_session_release_generation(host_link_writer_t writer, uint32_t generation);

/** @brief True if @p writer currently owns the session. */
bool host_link_session_owns(host_link_writer_t writer);

/**
 * @brief Queue received transport bytes without waiting. Only the core worker
 *
 * reassembles/dispatches. On overflow invalidate this generation, never
 *        splice across a
 * lost chunk. Callers must retry with a fresh HELLO.
 */
// Callback-safe, nonblocking ingress. A generation is captured BEFORE reading
// transport bytes, never looked up later when draining a queue.
uint32_t host_link_session_generation(host_link_writer_t writer);
bool host_link_generation_matches(host_link_writer_t writer, uint32_t generation);
bool host_link_feed(host_link_writer_t writer,
                    uint32_t generation,
                    const uint8_t *data,
                    size_t len);

// Capture when an asynchronous operation/log is STARTED, retain with its data.
// Zero means no authenticated, ready owner. Never retag old data on emission.
uint32_t host_link_output_generation(void);
void host_link_emit_stream_generation(
    uint32_t generation, uint8_t category, uint8_t op, const uint8_t *data, uint16_t len);
void host_link_emit_log_generation(uint32_t generation,
                                   host_log_source_t source,
                                   host_log_level_t level,
                                   const char *text,
                                   size_t text_len);

#ifdef HOST_LINK_TEST
bool host_link_test_pump(void);
#endif

/**
 * @brief Invalidate the transcript while retaining transport ownership. The
 *        worker
 * discards RX and keys asynchronously; requires a fresh HELLO.
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
/** True while BLE owns the single companion session. */
bool host_link_is_ble_transport(void);

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
