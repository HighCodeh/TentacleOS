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


#ifndef PROTOCOL_SAMSUNG32_H
#define PROTOCOL_SAMSUNG32_H

#include "driver/rmt_encoder.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Estrutura de scan code Samsung (32 bits)
 */
typedef struct {
    uint32_t data;  ///< Dados Samsung completos (32 bits)
} ir_samsung32_scan_code_t;

/**
 * @brief Timings do protocolo Samsung (em microsegundos)
 */
#define SAMSUNG32_LEADING_CODE_DURATION_0  4500
#define SAMSUNG32_LEADING_CODE_DURATION_1  4500
#define SAMSUNG32_PAYLOAD_ZERO_DURATION_0  560
#define SAMSUNG32_PAYLOAD_ZERO_DURATION_1  560
#define SAMSUNG32_PAYLOAD_ONE_DURATION_0   560
#define SAMSUNG32_PAYLOAD_ONE_DURATION_1   1690

#define EXAMPLE_IR_SAMSUNG32_DECODE_MARGIN 200  ///< Margem de erro para decodificação (us)

#ifdef __cplusplus
}
#endif

#endif // PROTOCOL_SAMSUNG32_H
