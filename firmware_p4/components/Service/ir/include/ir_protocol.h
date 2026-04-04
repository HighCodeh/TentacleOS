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

#ifndef IR_PROTOCOL_H
#define IR_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <driver/rmt_types.h>

#define IR_TOLERANCE 25

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IR_PROTO_UNKNOWN = 0,
    IR_PROTO_NEC,
    IR_PROTO_SAMSUNG,
    IR_PROTO_RC6,
    IR_PROTO_RC5,
    IR_PROTO_SONY,
    IR_PROTO_LG,
    IR_PROTO_JVC,
    IR_PROTO_DENON,
    IR_PROTO_PANASONIC,
} ir_protocol_t;

typedef struct {
    ir_protocol_t protocol;
    uint16_t      address;
    uint16_t      command;
    bool          repeat;
} ir_data_t;

bool        ir_match(uint32_t measured_us, uint32_t expected_us);
const char *ir_protocol_name(ir_protocol_t proto);
uint32_t    ir_carrier_freq(ir_protocol_t proto);

bool   ir_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out);
size_t ir_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

uint64_t ir_decode_pulse_distance(rmt_symbol_word_t *symbols, size_t offset, size_t num_bits,
                                  uint32_t one_space, uint32_t zero_space, bool msb_first);
uint64_t ir_decode_pulse_width(rmt_symbol_word_t *symbols, size_t offset, size_t num_bits,
                               uint32_t one_mark, uint32_t zero_mark, bool msb_first);

size_t ir_encode_pulse_distance(rmt_symbol_word_t *symbols,
                                uint32_t header_mark, uint32_t header_space,
                                uint32_t bit_mark, uint32_t one_space, uint32_t zero_space,
                                uint64_t data, size_t num_bits, bool msb_first, bool stop_bit);
size_t ir_encode_pulse_width(rmt_symbol_word_t *symbols,
                             uint32_t header_mark, uint32_t header_space,
                             uint32_t one_mark, uint32_t zero_mark, uint32_t bit_space,
                             uint64_t data, size_t num_bits, bool msb_first, bool stop_bit);

#ifdef __cplusplus
}
#endif

#endif // IR_PROTOCOL_H
