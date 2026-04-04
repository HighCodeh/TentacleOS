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

#ifndef IR_H
#define IR_H

#include "ir_protocol.h"

#define IR_RX_GPIO  6
#define IR_TX_GPIO  5

#ifdef __cplusplus
extern "C" {
#endif

void ir_rx_init(void);
void ir_tx_init(void);

bool ir_receive(ir_data_t *out, uint32_t timeout_ms);
void ir_send(const ir_data_t *data);
void ir_send_raw(const rmt_symbol_word_t *symbols, size_t count, uint32_t carrier_hz);

const rmt_symbol_word_t *ir_last_raw(size_t *count);

void ir_print_raw(rmt_symbol_word_t *symbols, size_t count);
void ir_print_data(const ir_data_t *data);

#ifdef __cplusplus
}
#endif

#endif // IR_H
