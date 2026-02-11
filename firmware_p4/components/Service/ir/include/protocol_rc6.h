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


#ifndef PROTOCOL_RC6_H
#define PROTOCOL_RC6_H

#include "driver/rmt_encoder.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Estrutura de scan code RC6
 */
typedef struct {
    uint8_t address;  ///< Endereço RC6 (8 bits)
    uint8_t command;  ///< Comando RC6 (8 bits)
    uint8_t toggle;   ///< Bit de toggle RC6 (0 ou 1)
} ir_rc6_scan_code_t;

/**
 * @brief Timings do protocolo RC6 (em microsegundos)
 */
#define RC6_LEADING_CODE_DURATION_0   2666
#define RC6_LEADING_CODE_DURATION_1   889
#define RC6_PAYLOAD_ZERO_DURATION_0   444
#define RC6_PAYLOAD_ZERO_DURATION_1   444
#define RC6_PAYLOAD_ONE_DURATION_0    444
#define RC6_PAYLOAD_ONE_DURATION_1    444
#define RC6_TOGGLE_DURATION_0         889
#define RC6_TOGGLE_DURATION_1         889

#define EXAMPLE_IR_RC6_DECODE_MARGIN  200  ///< Margem de erro para decodificação (us)

#ifdef __cplusplus
}
#endif

#endif // PROTOCOL_RC6_H
