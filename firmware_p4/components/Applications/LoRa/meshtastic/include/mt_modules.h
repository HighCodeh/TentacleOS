// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MT_MODULES_H
#define MT_MODULES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Meshtastic portnum identifiers.
 *
 * Values match PortNum in the official protobuf (portnums.proto).
 */
typedef enum {
  MT_PORT_UNKNOWN = 0,
  MT_PORT_TEXT_MESSAGE = 1,
  MT_PORT_REMOTE_HARDWARE = 2,
  MT_PORT_POSITION = 3,
  MT_PORT_NODEINFO = 4,
  MT_PORT_ROUTING = 5,
  MT_PORT_ADMIN = 6,
  MT_PORT_TEXT_COMPRESSED = 7,
  MT_PORT_WAYPOINT = 8,
  MT_PORT_DETECTION_SENSOR = 10,
  MT_PORT_ALERT = 11,
  MT_PORT_KEY_VERIFICATION = 12,
  MT_PORT_TELEMETRY = 67,
  MT_PORT_TRACEROUTE = 70,
  MT_PORT_NEIGHBORINFO = 71,
} mt_portnum_t;

/**
 * @brief Per-packet metadata passed to each module handler.
 */
typedef struct {
  uint32_t from;
  uint32_t to;
  uint32_t id;
  uint8_t channel;
  uint8_t hop_limit;
  uint8_t hop_start;
  int16_t rssi_dbm;
  int8_t snr_db;
  bool want_ack;
  bool want_response;
  uint32_t request_id;
} mt_packet_meta_t;

/**
 * @brief Initialize every built-in module.
 *
 * Must be called once at boot, after mesh init. Opens NVS via
 * mt_nvs_init(), then constructs Admin, NodeInfo, Position, Text,
 * Routing, TraceRoute, and Telemetry modules.
 *
 * @param node_num  Our node number (uint32, MAC-derived).
 * @return
 *   - ESP_OK on success
 *   - esp_err_t from mt_nvs_init on failure
 */
esp_err_t mt_modules_init(uint32_t node_num);

/**
 * @brief Dispatch a decrypted Data packet to the correct module.
 *
 * Called by the mesh core after AES-CTR decrypt. Routes by portnum to
 * the module that owns it and also triggers automatic ACK generation
 * when meta->want_ack is set and meta->to is us.
 *
 * @param meta       Packet metadata.
 * @param data_bytes Decrypted Data protobuf payload.
 * @param data_len   Payload size in bytes.
 */
void mt_modules_dispatch(const mt_packet_meta_t *meta,
                         const uint8_t *data_bytes,
                         uint16_t data_len);

/**
 * @brief Periodic tick for modules that need timer-driven behavior.
 *
 * Call once per second from the main loop. Routes to NodeInfo, Position,
 * Telemetry, and Admin tickers.
 */
void mt_modules_tick(void);

/**
 * @brief Minimal protobuf parser that extracts fields from a Data message.
 *
 * Decodes portnum (field 1), payload (field 2), and request_id (field 3).
 *
 * @param data_bytes       Raw Data protobuf bytes.
 * @param data_len         Size of data_bytes.
 * @param[out] out_portnum   Decoded portnum or 0 if absent.
 * @param[out] out_payload   Pointer into data_bytes where payload starts.
 * @param[out] out_payload_len Payload length.
 * @param[out] out_request_id  Decoded request_id or 0 if absent. May be NULL.
 * @return true on successful parse, false on malformed input.
 */
bool mt_parse_data(const uint8_t *data_bytes,
                   uint16_t data_len,
                   uint32_t *out_portnum,
                   const uint8_t **out_payload,
                   uint16_t *out_payload_len,
                   uint32_t *out_request_id);

#ifdef __cplusplus
}
#endif

#endif // MT_MODULES_H
