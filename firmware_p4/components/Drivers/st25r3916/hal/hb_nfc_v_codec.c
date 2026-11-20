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

#include "hb_nfc_v_codec.h"

#include <string.h>

#include "esp_log.h"

#define SAMPLE_US_NUMERATOR    12800U
#define SAMPLE_US_DENOMINATOR  1356U
#define SAMPLE_HALF_NUMERATOR  6400U
#define EDGE_WINDOW_US         6000000U
#define SOF_1OF4               0x21U
#define SOF_1OF256             0x81U
#define EOF_FRAME              0x04U
#define EOF_SLOT               0x01U
#define PAIRS_PER_BYTE         4U
#define PAIR_BITS              2U
#define GROUPS_1OF256          64U
#define BYTE_BITS              8U
#define LOW_RATE_COEFFICIENT   4U
#define SUBCARRIER_HALF_CYCLES 16U
#define HALF_BIT_CYCLES        256U
#define HEADER_BURST_CYCLES    768U
#define HEADER_PHASES          3U
#define DATA_PHASES_PER_BIT    2U

static const char *TAG = "NFC_V_CODEC";

static const uint8_t s_pair_patterns[PAIRS_PER_BYTE] = {0x02, 0x08, 0x20, 0x80};

static hb_nfc_v_codec_rx_result_t reject(hb_nfc_v_codec_rx_t *rx,
                                         hb_nfc_v_codec_rx_result_t result) {
  rx->length = 0;
  rx->result = result;
  return result;
}

esp_err_t hb_nfc_v_codec_rx_init(hb_nfc_v_codec_rx_t *rx, uint8_t *buffer, size_t capacity) {
  if (!rx)
    return ESP_ERR_INVALID_ARG;
  memset(rx, 0, sizeof(*rx));
  rx->result = HB_NFC_V_CODEC_RX_INVALID;
  if (!buffer || !capacity || capacity > HB_NFC_V_CODEC_MAX_FRAME) {
    ESP_LOGE(TAG, "Invalid NFC-V receive buffer");
    return ESP_ERR_INVALID_ARG;
  }
  rx->buffer = buffer;
  rx->capacity = capacity;
  rx->result = HB_NFC_V_CODEC_RX_WAIT;
  return ESP_OK;
}

hb_nfc_v_codec_rx_result_t hb_nfc_v_codec_rx_feed(hb_nfc_v_codec_rx_t *rx, uint8_t samples) {
  if (!rx)
    return HB_NFC_V_CODEC_RX_INVALID;
  if (rx->result != HB_NFC_V_CODEC_RX_WAIT)
    return rx->result;
  if (!rx->has_sof) {
    if (samples == EOF_SLOT) {
      rx->result = HB_NFC_V_CODEC_RX_SLOT;
    } else if (samples == SOF_1OF4 || samples == SOF_1OF256) {
      rx->has_sof = true;
      rx->is_1of256 = samples == SOF_1OF256;
    } else {
      return reject(rx, HB_NFC_V_CODEC_RX_INVALID);
    }
    return rx->result;
  }
  if (samples == EOF_FRAME) {
    if (rx->part || !rx->length)
      return reject(rx, HB_NFC_V_CODEC_RX_INVALID);
    rx->result = HB_NFC_V_CODEC_RX_FRAME;
    return rx->result;
  }
  if (rx->length == rx->capacity)
    return reject(rx, HB_NFC_V_CODEC_RX_OVERFLOW);
  if (!samples && rx->is_1of256) {
    /* Empty groups are part of the 512-sample symbol, not a frame boundary. */
  } else {
    uint8_t pair = 0;
    while (pair < PAIRS_PER_BYTE && samples != s_pair_patterns[pair])
      pair++;
    if (pair == PAIRS_PER_BYTE || (rx->is_1of256 && rx->has_pulse))
      return reject(rx, HB_NFC_V_CODEC_RX_INVALID);
    if (rx->is_1of256) {
      rx->byte = (uint8_t)(rx->part * PAIRS_PER_BYTE + pair);
      rx->has_pulse = true;
    } else {
      rx->byte |= (uint8_t)(pair << (rx->part * PAIR_BITS));
    }
  }
  rx->part++;
  if (rx->part == (rx->is_1of256 ? GROUPS_1OF256 : PAIRS_PER_BYTE)) {
    if (rx->is_1of256 && !rx->has_pulse)
      return reject(rx, HB_NFC_V_CODEC_RX_INVALID);
    rx->buffer[rx->length++] = rx->byte;
    rx->byte = 0;
    rx->part = 0;
    rx->has_pulse = false;
  }
  return rx->result;
}

esp_err_t hb_nfc_v_codec_tx_init(hb_nfc_v_codec_tx_t *tx,
                                 const uint8_t *frame,
                                 size_t length,
                                 hb_nfc_v_codec_rate_t rate,
                                 bool sof_only) {
  if (!tx)
    return ESP_ERR_INVALID_ARG;
  memset(tx, 0, sizeof(*tx));
  if ((unsigned)rate >= HB_NFC_V_CODEC_RATE_COUNT || length > HB_NFC_V_CODEC_MAX_FRAME ||
      (sof_only && length) || (!sof_only && (!frame || !length)))
    return ESP_ERR_INVALID_ARG;
  tx->frame = frame;
  tx->length = length;
  tx->coefficient = (rate == HB_NFC_V_CODEC_RATE_LOW || rate == HB_NFC_V_CODEC_RATE_FAST_LOW)
                        ? LOW_RATE_COEFFICIENT
                        : 1;
  tx->is_fast = rate == HB_NFC_V_CODEC_RATE_FAST_HIGH || rate == HB_NFC_V_CODEC_RATE_FAST_LOW;
  tx->phase_count =
      sof_only ? HEADER_PHASES : HEADER_PHASES * 2 + length * BYTE_BITS * DATA_PHASES_PER_BIT;
  return ESP_OK;
}

bool hb_nfc_v_codec_tx_next(hb_nfc_v_codec_tx_t *tx, bool *out_level, uint16_t *out_cycles) {
  if (!tx || !out_level || !out_cycles || tx->phase >= tx->phase_count)
    return false;
  bool quiet;
  uint16_t cycles;
  size_t data_end = HEADER_PHASES + tx->length * BYTE_BITS * DATA_PHASES_PER_BIT;
  if (tx->phase < HEADER_PHASES) {
    quiet = tx->phase == 1;
    cycles = tx->phase == 0 ? HEADER_BURST_CYCLES : HALF_BIT_CYCLES;
  } else if (tx->phase >= data_end) {
    size_t eof_phase = tx->phase - data_end;
    quiet = eof_phase == 1;
    cycles = eof_phase == HEADER_PHASES - 1 ? HEADER_BURST_CYCLES : HALF_BIT_CYCLES;
  } else {
    size_t half = tx->phase - HEADER_PHASES;
    size_t bit = half / DATA_PHASES_PER_BIT;
    bool one = (tx->frame[bit / BYTE_BITS] >> (bit % BYTE_BITS)) & 1U;
    quiet = one ? !(half % DATA_PHASES_PER_BIT) : half % DATA_PHASES_PER_BIT;
    cycles = HALF_BIT_CYCLES;
  }
  cycles *= tx->coefficient;
  if (tx->is_fast)
    cycles /= DATA_PHASES_PER_BIT;
  if (quiet) {
    *out_level = false;
    *out_cycles = cycles;
    tx->phase++;
  } else {
    *out_level = !(tx->edge % 2);
    *out_cycles = SUBCARRIER_HALF_CYCLES;
    if (++tx->edge == cycles / SUBCARRIER_HALF_CYCLES) {
      tx->edge = 0;
      tx->phase++;
    }
  }
  return true;
}

esp_err_t
hb_nfc_v_codec_edges_init(hb_nfc_v_codec_edges_t *edges, uint8_t *buffer, size_t capacity) {
  if (edges == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(edges, 0, sizeof(*edges));
  return hb_nfc_v_codec_rx_init(&edges->decoder, buffer, capacity);
}

hb_nfc_v_codec_rx_result_t hb_nfc_v_codec_edges_advance(hb_nfc_v_codec_edges_t *edges,
                                                        uint64_t now_us) {
  if (edges == NULL)
    return HB_NFC_V_CODEC_RX_INVALID;
  if (!edges->is_started || edges->decoder.result != HB_NFC_V_CODEC_RX_WAIT)
    return edges->decoder.result;
  if (now_us < edges->last_us || now_us - edges->origin_us > EDGE_WINDOW_US)
    return reject(&edges->decoder, HB_NFC_V_CODEC_RX_INVALID);
  uint64_t elapsed = (now_us - edges->origin_us) * SAMPLE_US_DENOMINATOR;
  while (edges->sample_index * SAMPLE_US_NUMERATOR + SAMPLE_HALF_NUMERATOR < elapsed) {
    if (edges->level)
      edges->group |= (uint8_t)(1U << edges->bits);
    edges->bits++;
    edges->sample_index++;
    if (edges->bits == BYTE_BITS) {
      hb_nfc_v_codec_rx_feed(&edges->decoder, edges->group);
      edges->bits = 0;
      edges->group = 0;
      if (edges->decoder.result != HB_NFC_V_CODEC_RX_WAIT)
        break;
    }
  }
  edges->last_us = now_us;
  return edges->decoder.result;
}

hb_nfc_v_codec_rx_result_t
hb_nfc_v_codec_edges_feed(hb_nfc_v_codec_edges_t *edges, uint64_t time_us, bool level) {
  if (edges == NULL)
    return HB_NFC_V_CODEC_RX_INVALID;
  if (!edges->is_started) {
    if (!level)
      return edges->decoder.result;
    edges->is_started = true;
    edges->origin_us = time_us;
    edges->last_us = time_us;
  } else {
    if (edges->level == level)
      return reject(&edges->decoder, HB_NFC_V_CODEC_RX_INVALID);
    hb_nfc_v_codec_edges_advance(edges, time_us);
  }
  edges->level = level;
  if (!level)
    edges->fall_us = time_us;
  return edges->decoder.result;
}
