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

#include "ir_protocol.h"

#include <string.h>

#include <esp_log.h>

#include "ir_protocol_nec.h"
#include "ir_protocol_samsung.h"
#include "ir_protocol_rc6.h"
#include "ir_protocol_rc5.h"
#include "ir_protocol_sony.h"
#include "ir_protocol_lg.h"
#include "ir_protocol_jvc.h"
#include "ir_protocol_denon.h"
#include "ir_protocol_panasonic.h"

static const char *TAG = "IR_PROTOCOL";

// ── Matching (25% tolerance) ──
bool ir_match(uint32_t measured, uint32_t expected) {
    uint32_t margin = expected * IR_TOLERANCE / 100;
    return measured >= (expected - margin) && measured <= (expected + margin);
}

const char *ir_protocol_name(ir_protocol_t proto) {
    switch (proto) {
        case IR_PROTO_NEC:       return "NEC";
        case IR_PROTO_SAMSUNG:   return "SAMSUNG";
        case IR_PROTO_RC6:       return "RC6";
        case IR_PROTO_RC5:       return "RC5";
        case IR_PROTO_SONY:      return "SONY";
        case IR_PROTO_LG:        return "LG";
        case IR_PROTO_JVC:       return "JVC";
        case IR_PROTO_DENON:     return "DENON";
        case IR_PROTO_PANASONIC: return "PANASONIC";
        default:                 return "UNKNOWN";
    }
}

uint32_t ir_carrier_freq(ir_protocol_t proto) {
    switch (proto) {
        case IR_PROTO_RC5:
        case IR_PROTO_RC6:       return 36000;
        case IR_PROTO_SONY:      return 40000;
        case IR_PROTO_PANASONIC: return 37000;
        default:                 return 38000;
    }
}

// ══════════════════════════════════════════════════════
// Generic decoders
// ══════════════════════════════════════════════════════

uint64_t ir_decode_pulse_distance(rmt_symbol_word_t *symbols, size_t offset, size_t num_bits,
                                  uint32_t one_space, uint32_t zero_space, bool msb_first) {
    uint64_t raw = 0;
    for (size_t i = 0; i < num_bits; i++) {
        rmt_symbol_word_t *s = &symbols[offset + i];
        bool is_one = ir_match(s->duration1, one_space);
        if (msb_first) {
            raw = (raw << 1) | (is_one ? 1 : 0);
        } else {
            if (is_one) raw |= (1ULL << i);
        }
    }
    return raw;
}

uint64_t ir_decode_pulse_width(rmt_symbol_word_t *symbols, size_t offset, size_t num_bits,
                               uint32_t one_mark, uint32_t zero_mark, bool msb_first) {
    uint64_t raw = 0;
    for (size_t i = 0; i < num_bits; i++) {
        rmt_symbol_word_t *s = &symbols[offset + i];
        bool is_one = ir_match(s->duration0, one_mark);
        if (msb_first) {
            raw = (raw << 1) | (is_one ? 1 : 0);
        } else {
            if (is_one) raw |= (1ULL << i);
        }
    }
    return raw;
}

// ══════════════════════════════════════════════════════
// Generic encoders
// ══════════════════════════════════════════════════════

size_t ir_encode_pulse_distance(rmt_symbol_word_t *symbols,
                                uint32_t header_mark, uint32_t header_space,
                                uint32_t bit_mark, uint32_t one_space, uint32_t zero_space,
                                uint64_t data, size_t num_bits, bool msb_first, bool stop_bit) {
    size_t idx = 0;

    if (header_mark > 0) {
        symbols[idx].duration0 = header_mark;  symbols[idx].level0 = 1;
        symbols[idx].duration1 = header_space;  symbols[idx].level1 = 0;
        idx++;
    }

    for (size_t i = 0; i < num_bits; i++) {
        int bit = msb_first ? (data >> (num_bits - 1 - i)) & 1
                            : (data >> i) & 1;
        symbols[idx].duration0 = bit_mark;          symbols[idx].level0 = 1;
        symbols[idx].duration1 = bit ? one_space : zero_space;  symbols[idx].level1 = 0;
        idx++;
    }

    if (stop_bit) {
        symbols[idx].duration0 = bit_mark;  symbols[idx].level0 = 1;
        symbols[idx].duration1 = 0;         symbols[idx].level1 = 0;
        idx++;
    }

    return idx;
}

size_t ir_encode_pulse_width(rmt_symbol_word_t *symbols,
                             uint32_t header_mark, uint32_t header_space,
                             uint32_t one_mark, uint32_t zero_mark, uint32_t bit_space,
                             uint64_t data, size_t num_bits, bool msb_first, bool stop_bit) {
    size_t idx = 0;

    if (header_mark > 0) {
        symbols[idx].duration0 = header_mark;  symbols[idx].level0 = 1;
        symbols[idx].duration1 = header_space;  symbols[idx].level1 = 0;
        idx++;
    }

    for (size_t i = 0; i < num_bits; i++) {
        int bit = msb_first ? (data >> (num_bits - 1 - i)) & 1
                            : (data >> i) & 1;
        symbols[idx].duration0 = bit ? one_mark : zero_mark;  symbols[idx].level0 = 1;
        symbols[idx].duration1 = bit_space;                     symbols[idx].level1 = 0;
        idx++;
    }

    if (stop_bit) {
        symbols[idx].duration0 = zero_mark;  symbols[idx].level0 = 1;
        symbols[idx].duration1 = 0;          symbols[idx].level1 = 0;
        idx++;
    }

    return idx;
}

// ══════════════════════════════════════════════════════
// Pipeline
// ══════════════════════════════════════════════════════

bool ir_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out) {
    memset(out, 0, sizeof(ir_data_t));

    // Protocols with long header first (most distinctive)
    if (nec_decode(symbols, count, out))       return true;
    if (lg_decode(symbols, count, out))        return true;
    if (jvc_decode(symbols, count, out))       return true;
    if (samsung_decode(symbols, count, out))   return true;
    if (panasonic_decode(symbols, count, out)) return true;
    if (rc6_decode(symbols, count, out))       return true;
    if (sony_decode(symbols, count, out))      return true;

    // Headerless protocols last
    if (rc5_decode(symbols, count, out))       return true;
    if (denon_decode(symbols, count, out))     return true;

    out->protocol = IR_PROTO_UNKNOWN;
    return false;
}

size_t ir_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
    switch (data->protocol) {
        case IR_PROTO_NEC:       return nec_encode(data, symbols, max);
        case IR_PROTO_SAMSUNG:   return samsung_encode(data, symbols, max);
        case IR_PROTO_RC6:       return rc6_encode(data, symbols, max);
        case IR_PROTO_RC5:       return rc5_encode(data, symbols, max);
        case IR_PROTO_SONY:      return sony_encode(data, symbols, max);
        case IR_PROTO_LG:        return lg_encode(data, symbols, max);
        case IR_PROTO_JVC:       return jvc_encode(data, symbols, max);
        case IR_PROTO_DENON:     return denon_encode(data, symbols, max);
        case IR_PROTO_PANASONIC: return panasonic_encode(data, symbols, max);
        default:                 return 0;
    }
}
