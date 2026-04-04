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
#include "ir_protocol_sony.h"

static const char *TAG = "IR_SONY";

bool sony_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out) {
    if (!ir_match(symbols[0].duration0, SONY_HEADER_MARK))
        return false;

    // Sony: 12, 15 or 20 data bits + header (no stop bit)
    size_t data_bits;
    if (count >= 21) data_bits = 20;       // Sony20
    else if (count >= 16) data_bits = 15;  // Sony15
    else if (count >= 13) data_bits = 12;  // Sony12
    else return false;

    if (!ir_match(symbols[0].duration1, SONY_HEADER_SPACE))
        return false;

    // Pulse-width: check mark duration
    uint64_t raw = ir_decode_pulse_width(symbols, 1, data_bits,
                                         SONY_ONE_MARK, SONY_ZERO_MARK, false);

    // LSB first: command(7) + address(5/8/13)
    out->protocol = IR_PROTO_SONY;
    out->command  = raw & 0x7F;
    out->address  = (raw >> 7) & 0x1FFF;
    out->repeat   = false;

    return true;
}

size_t sony_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
    // Auto-detect bit count: Sony12/15/20
    size_t num_bits;
    if (data->address > 0xFF)      num_bits = 20;
    else if (data->address > 0x1F) num_bits = 15;
    else                           num_bits = 12;

    uint64_t raw = (data->command & 0x7F) | ((uint64_t)(data->address) << 7);

    return ir_encode_pulse_width(symbols,
        SONY_HEADER_MARK, SONY_HEADER_SPACE,
        SONY_ONE_MARK, SONY_ZERO_MARK, SONY_BIT_SPACE,
        raw, num_bits, false, false);  // Sony: no stop bit
}
