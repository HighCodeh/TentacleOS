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
 * @file lora_session.h
 * @brief One-stack LoRa mesh session manager and unified UI-facing view.
 *
 * The SX1262 radio is single-owner, so Meshtastic and MeshCore cannot run at the
 * same time. This module starts the chosen stack (which brings up the radio
 * itself), tracks which one is running, and exposes a protocol-agnostic view the
 * chat hub reads: a local ring of sent/received text messages and a node list.
 * Switching protocols requires a reboot (there is no clean whole-stack stop).
 */

#ifndef LORA_SESSION_H
#define LORA_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
  LORA_PROTO_NONE = 0,
  LORA_PROTO_MESHTASTIC,
  LORA_PROTO_MESHCORE,
} lora_proto_t;

/** @brief One entry in the local chat ring. */
typedef struct {
  bool outgoing;
  char who[24];
  char text[160];
} lora_msg_t;

/** @brief Protocol-agnostic node/contact summary. */
typedef struct {
  char name[32];
  int16_t rssi;
  float snr;
} lora_node_t;

/**
 * @brief Start the selected mesh stack (idempotent for the same protocol).
 *
 * The stack's app_start brings up the SX1262 itself. The radio is single-owner,
 * so once a protocol is running a request for a different one is refused.
 *
 * @param proto  Protocol to start.
 * @return ESP_OK if started (or already running that protocol);
 *         ESP_ERR_INVALID_STATE if a different protocol already owns the radio;
 *         an esp_err_t from the stack on failure.
 */
esp_err_t lora_session_start(lora_proto_t proto);

/** @brief Protocol currently owning the radio, or LORA_PROTO_NONE. */
lora_proto_t lora_session_active(void);

/**
 * @brief Send a broadcast / public-channel text on the active protocol.
 *
 * The message is also appended to the local chat ring as outgoing.
 *
 * @param text  UTF-8 text to send.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if no stack is running.
 */
esp_err_t lora_session_send_text(const char *text);

/** @brief Number of messages currently held in the local chat ring. */
uint16_t lora_session_msg_count(void);

/**
 * @brief Atomically copy messages newer than a caller-held sequence cursor.
 *
 * Reads the ring under a single lock so the (oldest, total) window stays
 * consistent even while the mesh poll task pushes concurrently. Advances
 * @p io_seq past the copied messages; on first use pass 0 and it snaps forward
 * to the oldest message still held.
 *
 * @param io_seq  In/out absolute sequence cursor.
 * @param out     Destination buffer.
 * @param max     Capacity of @p out.
 * @return Number of messages copied (call again while it returns @p max).
 */
uint16_t lora_session_msg_since(uint32_t *io_seq, lora_msg_t *out, uint16_t max);

/** @brief Number of known nodes/contacts on the active protocol. */
uint16_t lora_session_node_count(void);

/**
 * @brief Copy a node/contact summary by index.
 * @return true if @p idx is valid.
 */
bool lora_session_node_get(uint16_t idx, lora_node_t *out);

/**
 * @brief Append a received message to the local chat ring.
 *
 * Called from the mesh poll task (NOT the LVGL thread) by the backend RX taps.
 *
 * @param who   Short sender label.
 * @param text  Received text.
 */
void lora_session_on_rx_text(const char *who, const char *text);

/** @brief True if a companion app is connected over the phone bridge (C5). */
bool lora_session_app_connected(void);

/**
 * @brief Start phone-bridge BLE advertising for the active protocol.
 *
 * Idempotent; the transport terminates on the C5 co-processor.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if no stack is running.
 */
esp_err_t lora_session_app_connect(void);

#ifdef __cplusplus
}
#endif

#endif // LORA_SESSION_H
