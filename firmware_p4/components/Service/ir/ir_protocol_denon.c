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
#include "ir_protocol_denon.h"

static const char *TAG = "IR_DENON";

bool denon_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out) {
    // Denon: no header, first mark ~260us (very short)
    if (count < 16) return false;
    if (symbols[0].duration0 > 600) return false;

    // Validate short marks in first symbols
    for (int i = 0; i < 3; i++) {
        if (!ir_match(symbols[i].duration0, DENON_BIT_MARK))
            return false;
    }

    // 15 bits LSB first
    uint32_t raw = (uint32_t)ir_decode_pulse_distance(symbols, 0, 15,
                                                       DENON_ONE_SPACE, DENON_ZERO_SPACE, false);

    out->protocol = IR_PROTO_DENON;
    out->address  = raw & 0x1F;
    out->command  = (raw >> 5) & 0xFF;
    out->repeat   = false;

    return true;
}

size_t denon_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
    uint16_t raw = (data->address & 0x1F) | ((uint16_t)(data->command & 0xFF) << 5);

    return ir_encode_pulse_distance(symbols,
        0, 0,
        DENON_BIT_MARK, DENON_ONE_SPACE, DENON_ZERO_SPACE,
        raw, 15, false, true);
}
