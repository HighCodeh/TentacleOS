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

#ifndef IR_PROTOCOL_SAMSUNG_H
#define IR_PROTOCOL_SAMSUNG_H

#include "ir_protocol.h"

#define SAMSUNG_HEADER_MARK   4500
#define SAMSUNG_HEADER_SPACE  4500
#define SAMSUNG_BIT_MARK       560
#define SAMSUNG_ONE_SPACE     1690
#define SAMSUNG_ZERO_SPACE     560

#ifdef __cplusplus
extern "C" {
#endif

bool   samsung_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out);
size_t samsung_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

#ifdef __cplusplus
}
#endif

#endif
