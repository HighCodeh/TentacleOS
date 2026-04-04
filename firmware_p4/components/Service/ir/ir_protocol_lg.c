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
#include "ir_protocol_lg.h"

static const char *TAG = "IR_LG";

static uint8_t lg_checksum(uint16_t command) {
    uint8_t sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += (command >> (i * 4)) & 0xF;
    }
    return sum & 0xF;
}

bool lg_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out) {
    // LG repeat: header mark + short space + stop
    if (count <= 4 && ir_match(symbols[0].duration0, LG_HEADER_MARK) &&
        ir_match(symbols[0].duration1, LG_REPEAT_SPACE)) {
        out->protocol = IR_PROTO_LG;
        out->repeat = true;
        return true;
    }

    if (count < 30) return false;
    if (!ir_match(symbols[0].duration0, LG_HEADER_MARK) ||
        !ir_match(symbols[0].duration1, LG_HEADER_SPACE))
        return false;

    // 28 bits MSB first
    uint32_t raw = (uint32_t)ir_decode_pulse_distance(symbols, 1, 28,
                                                       LG_ONE_SPACE, LG_ZERO_SPACE, true);

    uint8_t  addr = (raw >> 20) & 0xFF;
    uint16_t cmd  = (raw >> 4) & 0xFFFF;
    uint8_t  chk  = raw & 0xF;

    if (chk != lg_checksum(cmd)) return false;

    out->protocol = IR_PROTO_LG;
    out->address  = addr;
    out->command  = cmd;
    out->repeat   = false;

    return true;
}

size_t lg_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
    if (data->repeat) {
        symbols[0] = (rmt_symbol_word_t){ .duration0 = LG_HEADER_MARK, .level0 = 1,
                                          .duration1 = LG_REPEAT_SPACE, .level1 = 0 };
        symbols[1] = (rmt_symbol_word_t){ .duration0 = LG_BIT_MARK, .level0 = 1,
                                          .duration1 = 0, .level1 = 0 };
        return 2;
    }

    uint8_t chk = lg_checksum(data->command);
    uint32_t raw = ((uint32_t)(data->address & 0xFF) << 20) |
                   ((uint32_t)(data->command & 0xFFFF) << 4) |
                   (chk & 0xF);

    return ir_encode_pulse_distance(symbols,
        LG_HEADER_MARK, LG_HEADER_SPACE,
        LG_BIT_MARK, LG_ONE_SPACE, LG_ZERO_SPACE,
        raw, 28, true, true);
}
