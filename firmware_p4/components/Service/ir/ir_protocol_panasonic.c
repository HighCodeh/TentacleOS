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
#include "ir_protocol_panasonic.h"

static const char *TAG = "IR_PANASONIC";

bool panasonic_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out) {
    if (count < 50) return false;
    if (!ir_match(symbols[0].duration0, PANASONIC_HEADER_MARK) ||
        !ir_match(symbols[0].duration1, PANASONIC_HEADER_SPACE))
        return false;

    // 48 bits LSB first
    uint64_t raw = ir_decode_pulse_distance(symbols, 1, 48,
                                            PANASONIC_ONE_SPACE, PANASONIC_ZERO_SPACE, false);

    // Vendor ID (16 bits)
    uint16_t vendor = raw & 0xFFFF;
    if (vendor != PANASONIC_VENDOR_ID) return false;

    // Remaining 32 bits: vendorParity(4) + address(12) + command(8) + parity(8)
    uint8_t byte0 = (raw >> 16) & 0xFF;
    uint8_t byte1 = (raw >> 24) & 0xFF;
    uint8_t byte2 = (raw >> 32) & 0xFF;
    uint8_t byte3 = (raw >> 40) & 0xFF;

    // Verify 8-bit parity
    if (byte3 != (uint8_t)(byte0 ^ byte1 ^ byte2)) return false;

    out->protocol = IR_PROTO_PANASONIC;
    out->address  = ((byte0 >> 4) & 0x0F) | ((uint16_t)byte1 << 4);
    out->command  = byte2;
    out->repeat   = false;

    return true;
}

size_t panasonic_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
    uint16_t vendor = PANASONIC_VENDOR_ID;

    // Vendor parity
    uint8_t vp = vendor ^ (vendor >> 8);
    vp = (vp ^ (vp >> 4)) & 0x0F;

    uint8_t byte0 = (vp & 0x0F) | ((data->address & 0x0F) << 4);
    uint8_t byte1 = (data->address >> 4) & 0xFF;
    uint8_t byte2 = data->command & 0xFF;
    uint8_t byte3 = byte0 ^ byte1 ^ byte2;

    uint64_t raw = (uint64_t)vendor |
                   ((uint64_t)byte0 << 16) |
                   ((uint64_t)byte1 << 24) |
                   ((uint64_t)byte2 << 32) |
                   ((uint64_t)byte3 << 40);

    return ir_encode_pulse_distance(symbols,
        PANASONIC_HEADER_MARK, PANASONIC_HEADER_SPACE,
        PANASONIC_BIT_MARK, PANASONIC_ONE_SPACE, PANASONIC_ZERO_SPACE,
        raw, 48, false, true);
}
