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

#ifndef HB_NFC_V_CODEC_H
#define HB_NFC_V_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HB_NFC_V_CODEC_MAX_FRAME 1024U

typedef enum {
  HB_NFC_V_CODEC_RX_WAIT,
  HB_NFC_V_CODEC_RX_FRAME,
  HB_NFC_V_CODEC_RX_SLOT,
  HB_NFC_V_CODEC_RX_INVALID,
  HB_NFC_V_CODEC_RX_OVERFLOW,
  HB_NFC_V_CODEC_RX_COUNT
} hb_nfc_v_codec_rx_result_t;

typedef struct {
  uint8_t *buffer;
  size_t capacity;
  size_t length;
  uint8_t part;
  uint8_t byte;
  bool has_sof;
  bool is_1of256;
  bool has_pulse;
  hb_nfc_v_codec_rx_result_t result;
} hb_nfc_v_codec_rx_t;

typedef struct {
  hb_nfc_v_codec_rx_t decoder;
  uint64_t origin_us;
  uint64_t last_us;
  uint64_t fall_us;
  uint64_t sample_index;
  uint8_t group;
  uint8_t bits;
  bool is_started;
  bool level;
} hb_nfc_v_codec_edges_t;

/* Timestamped IRQ edges, high during a reader modulation pause. Advance only
 * after all edges up to now_us have been delivered. No GPIO or RTOS dependency. */
esp_err_t
hb_nfc_v_codec_edges_init(hb_nfc_v_codec_edges_t *edges, uint8_t *buffer, size_t capacity);
hb_nfc_v_codec_rx_result_t
hb_nfc_v_codec_edges_feed(hb_nfc_v_codec_edges_t *edges, uint64_t time_us, bool level);
hb_nfc_v_codec_rx_result_t hb_nfc_v_codec_edges_advance(hb_nfc_v_codec_edges_t *edges,
                                                        uint64_t now_us);

typedef enum {
  HB_NFC_V_CODEC_RATE_HIGH,
  HB_NFC_V_CODEC_RATE_LOW,
  HB_NFC_V_CODEC_RATE_FAST_HIGH,
  HB_NFC_V_CODEC_RATE_FAST_LOW,
  HB_NFC_V_CODEC_RATE_COUNT
} hb_nfc_v_codec_rate_t;

typedef struct {
  const uint8_t *frame;
  size_t length;
  size_t phase;
  size_t phase_count;
  uint16_t edge;
  uint8_t coefficient;
  bool is_fast;
} hb_nfc_v_codec_tx_t;

/* Feed one aligned group of eight samples, LSB first, at fc/128. CRC stays in
 * the output frame and must be checked by the protocol owner before dispatch. */
esp_err_t hb_nfc_v_codec_rx_init(hb_nfc_v_codec_rx_t *rx, uint8_t *buffer, size_t capacity);
hb_nfc_v_codec_rx_result_t hb_nfc_v_codec_rx_feed(hb_nfc_v_codec_rx_t *rx, uint8_t samples);

/* Incremental single-subcarrier response waveform. Frame includes its CRC.
 * No allocation or full waveform buffer; durations are in RF carrier cycles.
 * sof_only emits the standalone SOF response used by inventory commands. */
esp_err_t hb_nfc_v_codec_tx_init(hb_nfc_v_codec_tx_t *tx,
                                 const uint8_t *frame,
                                 size_t length,
                                 hb_nfc_v_codec_rate_t rate,
                                 bool sof_only);
bool hb_nfc_v_codec_tx_next(hb_nfc_v_codec_tx_t *tx, bool *out_level, uint16_t *out_cycles);

#ifdef __cplusplus
}
#endif
#endif
