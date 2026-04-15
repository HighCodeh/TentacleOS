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

#ifndef SX1262_TYPES_H
#define SX1262_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "sx1262_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief FSM states matching DS Fig. 9-1 / Table 9-1.
 */
typedef enum {
  SX1262_STATE_UNKNOWN = 0,    /**< Before any init.                    */
  SX1262_STATE_SLEEP = 1,      /**< Minimum consumption — DS Section 9.3.  */
  SX1262_STATE_STDBY_RC = 2,   /**< RC13M active — DS Section 9.4.        */
  SX1262_STATE_STDBY_XOSC = 3, /**< XOSC active — DS Section 9.4.        */
  SX1262_STATE_FS = 4,         /**< PLL locked — DS Section 9.5.          */
  SX1262_STATE_TX = 5,         /**< Transmitting — DS Section 9.7.        */
  SX1262_STATE_RX = 6,         /**< Receiving — DS Section 9.6.           */
} sx1262_state_t;

/**
 * @brief LoRa radio configuration parameters.
 */
typedef struct {
  sx1262_hal_t hal;       /**< Platform HAL — all callbacks.    */
  uint32_t frequency_hz;  /**< 150 000 000 – 960 000 000 Hz.   */
  uint8_t sf;             /**< Spreading factor: SX1262_LORA_SF5 … SF12. */
  uint8_t bw;             /**< Bandwidth: SX1262_LORA_BW_7 … BW_500.     */
  uint8_t cr;             /**< Coding rate: SX1262_LORA_CR_4_5 … CR_4_8. */
  int8_t tx_power_dbm;    /**< TX power: -9 to +22 dBm.        */
  uint16_t preamble_len;  /**< Preamble symbols. Min recommended: 12.     */
  bool is_crc_on;         /**< Enable CRC on payload.           */
  bool is_inverted_iq;    /**< true = LoRaWAN downlink (activates W4).    */
  bool is_implicit_hdr;   /**< true = Implicit Header mode (activates W3). */
  bool is_public_network; /**< true = public sync word (0x3444). */
} sx1262_config_t;

/**
 * @brief Received packet with payload and RF metadata.
 */
typedef struct {
  uint8_t buf[256];        /**< Payload — static buffer.         */
  uint8_t len;             /**< Valid bytes in buf.               */
  int16_t rssi_pkt_dbm;    /**< Packet RSSI in dBm.              */
  int8_t snr_pkt_db;       /**< SNR in dB.                       */
  int16_t signal_rssi_dbm; /**< Signal RSSI — DS Section 13.5.3. */
  bool has_crc_error;      /**< CRC check failed.                */
  bool has_header_error;   /**< Header corrupted.                */
} sx1262_packet_t;

/**
 * @brief Packet statistics counters. DS Section 13.5.5.
 */
typedef struct {
  uint16_t nb_pkt_received; /**< Total packets received.          */
  uint16_t nb_crc_error;    /**< Packets with CRC error.          */
  uint16_t nb_header_error; /**< Packets with header error.       */
} sx1262_stats_t;

/**
 * @brief Callback: TX completed successfully.
 * @param ctx  User context pointer.
 */
typedef void (*sx1262_tx_done_cb_t)(void *ctx);

/**
 * @brief Callback: packet received.
 * @param pkt  Pointer to received packet data.
 * @param ctx  User context pointer.
 */
typedef void (*sx1262_rx_done_cb_t)(const sx1262_packet_t *pkt, void *ctx);

/**
 * @brief Callback: CAD completed.
 * @param is_channel_active  true if LoRa activity detected.
 * @param ctx                User context pointer.
 */
typedef void (*sx1262_cad_done_cb_t)(bool is_channel_active, void *ctx);

/**
 * @brief Callback: RX or TX timeout.
 * @param ctx  User context pointer.
 */
typedef void (*sx1262_timeout_cb_t)(void *ctx);

/**
 * @brief Callback: hardware error (CRC or header error standalone).
 * @param err  Error code.
 * @param ctx  User context pointer.
 */
typedef void (*sx1262_error_cb_t)(int err, void *ctx);

/**
 * @brief Event callback registration struct.
 */
typedef struct {
  sx1262_tx_done_cb_t on_tx_done;   /**< TX done callback. May be NULL.   */
  sx1262_rx_done_cb_t on_rx_done;   /**< RX done callback. May be NULL.   */
  sx1262_cad_done_cb_t on_cad_done; /**< CAD done callback. May be NULL.  */
  sx1262_timeout_cb_t on_timeout;   /**< Timeout callback. May be NULL.   */
  sx1262_error_cb_t on_error;       /**< Error callback. May be NULL.     */
  void *cb_ctx;                     /**< User context for all callbacks.  */
} sx1262_callbacks_t;

#ifdef __cplusplus
}
#endif

#endif // SX1262_TYPES_H
