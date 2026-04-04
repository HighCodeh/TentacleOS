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
#include "ir_protocol_samsung.h"

static const char *TAG = "IR_SAMSUNG";

bool samsung_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out) {
    if (count < 34) return false;
    if (!ir_match(symbols[0].duration0, SAMSUNG_HEADER_MARK) ||
        !ir_match(symbols[0].duration1, SAMSUNG_HEADER_SPACE))
        return false;

    uint32_t raw = (uint32_t)ir_decode_pulse_distance(symbols, 1, 32,
                                                       SAMSUNG_ONE_SPACE, SAMSUNG_ZERO_SPACE, false);

    uint8_t addr_lo = (raw >>  0) & 0xFF;
    uint8_t addr_hi = (raw >>  8) & 0xFF;
    uint8_t cmd     = (raw >> 16) & 0xFF;
    uint8_t cmd_inv = (raw >> 24) & 0xFF;

    if ((uint8_t)(cmd ^ cmd_inv) != 0xFF) return false;

    out->protocol = IR_PROTO_SAMSUNG;
    out->command  = cmd;
    out->repeat   = false;
    out->address  = (addr_lo == addr_hi) ? addr_lo : (raw & 0xFFFF);

    return true;
}

size_t samsung_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
    uint8_t cmd = data->command & 0xFF;
    uint32_t raw;

    if (data->address <= 0xFF) {
        uint8_t addr = data->address & 0xFF;
        raw = addr | ((uint32_t)addr << 8) |
              ((uint32_t)cmd << 16) | ((uint32_t)(~cmd & 0xFF) << 24);
    } else {
        raw = data->address | ((uint32_t)cmd << 16) | ((uint32_t)(~cmd & 0xFF) << 24);
    }

    return ir_encode_pulse_distance(symbols,
        SAMSUNG_HEADER_MARK, SAMSUNG_HEADER_SPACE,
        SAMSUNG_BIT_MARK, SAMSUNG_ONE_SPACE, SAMSUNG_ZERO_SPACE,
        raw, 32, false, true);
}
