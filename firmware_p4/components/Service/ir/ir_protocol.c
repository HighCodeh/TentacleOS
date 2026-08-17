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

#include "ir_protocol.h"

#include <string.h>

#include "esp_log.h"

#include "ir_protocol_nec.h"
#include "ir_protocol_samsung.h"
#include "ir_protocol_rc6.h"
#include "ir_protocol_rc5.h"
#include "ir_protocol_sony.h"
#include "ir_protocol_lg.h"
#include "ir_protocol_jvc.h"
#include "ir_protocol_denon.h"
#include "ir_protocol_panasonic.h"
#include "ir_protocol_rca.h"
#include "ir_protocol_pioneer.h"
#include "ir_protocol_nec42.h"

static const char *TAG = "IR_PROTOCOL";

bool ir_match_tol(uint32_t measured, uint32_t expected, uint32_t tol_percent) {
  uint32_t margin = expected * tol_percent / 100;
  return measured >= (expected - margin) && measured <= (expected + margin);
}

bool ir_match(uint32_t measured, uint32_t expected) {
  return ir_match_tol(measured, expected, IR_TOLERANCE);
}

const char *ir_protocol_name(ir_protocol_t proto) {
  switch (proto) {
    case IR_PROTO_NEC:
      return "NEC";
    case IR_PROTO_SAMSUNG:
      return "SAMSUNG";
    case IR_PROTO_RC6:
      return "RC6";
    case IR_PROTO_RC5:
      return "RC5";
    case IR_PROTO_SONY:
      return "SONY";
    case IR_PROTO_LG:
      return "LG";
    case IR_PROTO_JVC:
      return "JVC";
    case IR_PROTO_DENON:
      return "DENON";
    case IR_PROTO_PANASONIC:
      return "PANASONIC";
    case IR_PROTO_RCA:
      return "RCA";
    case IR_PROTO_PIONEER:
      return "PIONEER";
    case IR_PROTO_NEC42:
      return "NEC42";
    default:
      return "UNKNOWN";
  }
}

uint32_t ir_carrier_freq(ir_protocol_t proto) {
  switch (proto) {
    case IR_PROTO_RC5:
    case IR_PROTO_RC6:
      return IR_CARRIER_HZ_RC5_RC6;
    case IR_PROTO_SONY:
      return IR_CARRIER_HZ_SONY;
    case IR_PROTO_PANASONIC:
      return IR_CARRIER_HZ_PANASONIC;
    case IR_PROTO_PIONEER:
      return IR_CARRIER_HZ_PIONEER;
    default:
      return IR_CARRIER_HZ_DEFAULT;
  }
}

uint64_t ir_decode_pulse_distance(const rmt_symbol_word_t *symbols,
                                  size_t offset,
                                  size_t num_bits,
                                  const ir_pulse_distance_cfg_t *cfg) {
  if (symbols == NULL || num_bits == 0 || cfg == NULL)
    return 0;

  uint64_t raw = 0;
  for (size_t i = 0; i < num_bits; i++) {
    const rmt_symbol_word_t *s = &symbols[offset + i];
    bool is_one = ir_match(s->duration1, cfg->one_space);
    if (cfg->msb_first) {
      raw = (raw << 1) | (is_one ? 1 : 0);
    } else {
      if (is_one)
        raw |= (1ULL << i);
    }
  }
  return raw;
}

uint64_t ir_decode_pulse_width(const rmt_symbol_word_t *symbols,
                               size_t offset,
                               size_t num_bits,
                               const ir_pulse_width_cfg_t *cfg) {
  if (symbols == NULL || num_bits == 0 || cfg == NULL)
    return 0;

  uint64_t raw = 0;
  for (size_t i = 0; i < num_bits; i++) {
    const rmt_symbol_word_t *s = &symbols[offset + i];
    bool is_one = ir_match(s->duration0, cfg->one_mark);
    if (cfg->msb_first) {
      raw = (raw << 1) | (is_one ? 1 : 0);
    } else {
      if (is_one)
        raw |= (1ULL << i);
    }
  }
  return raw;
}

size_t ir_encode_pulse_distance(rmt_symbol_word_t *symbols,
                                uint64_t data,
                                size_t num_bits,
                                const ir_encode_distance_cfg_t *cfg) {
  if (symbols == NULL || num_bits == 0 || cfg == NULL)
    return 0;

  size_t idx = 0;

  if (cfg->header_mark > 0) {
    if (idx >= cfg->max) {
      ESP_LOGE(TAG, "encode_pulse_distance: buffer overflow at header");
      return 0;
    }
    symbols[idx].duration0 = cfg->header_mark;
    symbols[idx].level0 = 1;
    symbols[idx].duration1 = cfg->header_space;
    symbols[idx].level1 = 0;
    idx++;
  }

  for (size_t i = 0; i < num_bits; i++) {
    if (idx >= cfg->max) {
      ESP_LOGE(TAG, "encode_pulse_distance: buffer overflow at bit %d", (int)i);
      return 0;
    }
    uint8_t bit = cfg->msb_first ? (data >> (num_bits - 1 - i)) & 1 : (data >> i) & 1;
    symbols[idx].duration0 = cfg->bit_mark;
    symbols[idx].level0 = 1;
    symbols[idx].duration1 = bit ? cfg->one_space : cfg->zero_space;
    symbols[idx].level1 = 0;
    idx++;
  }

  if (cfg->stop_bit) {
    if (idx >= cfg->max) {
      ESP_LOGE(TAG, "encode_pulse_distance: buffer overflow at stop bit");
      return 0;
    }
    symbols[idx].duration0 = cfg->bit_mark;
    symbols[idx].level0 = 1;
    symbols[idx].duration1 = 0;
    symbols[idx].level1 = 0;
    idx++;
  }

  return idx;
}

size_t ir_encode_pulse_width(rmt_symbol_word_t *symbols,
                             uint64_t data,
                             size_t num_bits,
                             const ir_encode_width_cfg_t *cfg) {
  if (symbols == NULL || num_bits == 0 || cfg == NULL)
    return 0;

  size_t idx = 0;

  if (cfg->header_mark > 0) {
    if (idx >= cfg->max) {
      ESP_LOGE(TAG, "encode_pulse_width: buffer overflow at header");
      return 0;
    }
    symbols[idx].duration0 = cfg->header_mark;
    symbols[idx].level0 = 1;
    symbols[idx].duration1 = cfg->header_space;
    symbols[idx].level1 = 0;
    idx++;
  }

  for (size_t i = 0; i < num_bits; i++) {
    if (idx >= cfg->max) {
      ESP_LOGE(TAG, "encode_pulse_width: buffer overflow at bit %d", (int)i);
      return 0;
    }
    uint8_t bit = cfg->msb_first ? (data >> (num_bits - 1 - i)) & 1 : (data >> i) & 1;
    symbols[idx].duration0 = bit ? cfg->one_mark : cfg->zero_mark;
    symbols[idx].level0 = 1;
    symbols[idx].duration1 = cfg->bit_space;
    symbols[idx].level1 = 0;
    idx++;
  }

  if (cfg->stop_bit) {
    if (idx >= cfg->max) {
      ESP_LOGE(TAG, "encode_pulse_width: buffer overflow at stop bit");
      return 0;
    }
    symbols[idx].duration0 = cfg->zero_mark;
    symbols[idx].level0 = 1;
    symbols[idx].duration1 = 0;
    symbols[idx].level1 = 0;
    idx++;
  }

  return idx;
}

bool ir_decode(const rmt_symbol_word_t *symbols, size_t count, ir_data_t *out_data) {
  if (symbols == NULL || count == 0 || out_data == NULL)
    return false;

  memset(out_data, 0, sizeof(ir_data_t));

  if (ir_protocol_pioneer_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_nec42_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_nec_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_lg_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_jvc_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_samsung_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_panasonic_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_rca_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_rc6_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_sony_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_rc5_decode(symbols, count, out_data))
    return true;
  if (ir_protocol_denon_decode(symbols, count, out_data))
    return true;

  out_data->protocol = IR_PROTO_UNKNOWN;
  ESP_LOGW(TAG, "No protocol matched (%d symbols)", (int)count);
  return false;
}

size_t ir_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
  if (data == NULL || symbols == NULL || max == 0)
    return 0;

  switch (data->protocol) {
    case IR_PROTO_NEC:
      return ir_protocol_nec_encode(data, symbols, max);
    case IR_PROTO_SAMSUNG:
      return ir_protocol_samsung_encode(data, symbols, max);
    case IR_PROTO_RC6:
      return ir_protocol_rc6_encode(data, symbols, max);
    case IR_PROTO_RC5:
      return ir_protocol_rc5_encode(data, symbols, max);
    case IR_PROTO_SONY:
      return ir_protocol_sony_encode(data, symbols, max);
    case IR_PROTO_LG:
      return ir_protocol_lg_encode(data, symbols, max);
    case IR_PROTO_JVC:
      return ir_protocol_jvc_encode(data, symbols, max);
    case IR_PROTO_DENON:
      return ir_protocol_denon_encode(data, symbols, max);
    case IR_PROTO_PANASONIC:
      return ir_protocol_panasonic_encode(data, symbols, max);
    case IR_PROTO_RCA:
      return ir_protocol_rca_encode(data, symbols, max);
    case IR_PROTO_PIONEER:
      return ir_protocol_pioneer_encode(data, symbols, max);
    case IR_PROTO_NEC42:
      return ir_protocol_nec42_encode(data, symbols, max);
    default:
      ESP_LOGW(TAG, "Encode called with unknown protocol: %d", (int)data->protocol);
      return 0;
  }
}