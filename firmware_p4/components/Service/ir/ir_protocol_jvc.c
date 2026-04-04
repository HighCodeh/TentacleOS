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
#include "ir_protocol_jvc.h"

static const char *TAG = "IR_JVC";

bool jvc_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out) {
    bool has_header = false;
    size_t data_offset = 0;

    // Full frame with header
    if (count >= 18 && ir_match(symbols[0].duration0, JVC_HEADER_MARK) &&
        ir_match(symbols[0].duration1, JVC_HEADER_SPACE)) {
        has_header = true;
        data_offset = 1;
    }
    // Repeat: no header, 16 bits + stop = 17 symbols, bit mark ~526us
    else if (count >= 17 && count <= 18 &&
             ir_match(symbols[0].duration0, JVC_BIT_MARK)) {
        // Validate that some marks match JVC timing
        for (int i = 0; i < 3 && i < (int)count; i++) {
            if (!ir_match(symbols[i].duration0, JVC_BIT_MARK))
                return false;
        }
        data_offset = 0;
    }
    else {
        return false;
    }

    if (count < data_offset + 17) return false;

    uint32_t raw = (uint32_t)ir_decode_pulse_distance(symbols, data_offset, 16,
                                                       JVC_ONE_SPACE, JVC_ZERO_SPACE, false);

    out->protocol = IR_PROTO_JVC;
    out->address  = raw & 0xFF;
    out->command  = (raw >> 8) & 0xFF;
    out->repeat   = !has_header;

    return true;
}

size_t jvc_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
    uint16_t raw = (data->address & 0xFF) | ((uint16_t)(data->command & 0xFF) << 8);

    return ir_encode_pulse_distance(symbols,
        data->repeat ? 0 : JVC_HEADER_MARK,
        data->repeat ? 0 : JVC_HEADER_SPACE,
        JVC_BIT_MARK, JVC_ONE_SPACE, JVC_ZERO_SPACE,
        raw, 16, false, true);
}
