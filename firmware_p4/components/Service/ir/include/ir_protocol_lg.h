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

#ifndef IR_PROTOCOL_LG_H
#define IR_PROTOCOL_LG_H

#include "ir_protocol.h"

#define LG_HEADER_MARK   8416
#define LG_HEADER_SPACE  4208
#define LG_BIT_MARK       526
#define LG_ONE_SPACE     1578
#define LG_ZERO_SPACE     550
#define LG_REPEAT_SPACE  2104

#ifdef __cplusplus
extern "C" {
#endif

bool   lg_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out);
size_t lg_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max);

#ifdef __cplusplus
}
#endif

#endif
