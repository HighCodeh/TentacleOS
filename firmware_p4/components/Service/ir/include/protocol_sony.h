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


#ifndef PROTOCOL_SONY_H
#define PROTOCOL_SONY_H

#include "driver/rmt_encoder.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Estrutura de scan code Sony SIRC
 */
typedef struct {
    uint16_t address;  ///< Endereço Sony (5, 8 ou 13 bits dependendo do modo)
    uint8_t command;   ///< Comando Sony (7 bits)
    uint8_t bits;      ///< Número de bits no frame (12, 15 ou 20)
} ir_sony_scan_code_t;

/**
 * @brief Timings do protocolo Sony SIRC (em microsegundos)
 */
#define SONY_LEADING_CODE_DURATION    2400
#define SONY_PAYLOAD_ZERO_DURATION    600
#define SONY_PAYLOAD_ONE_DURATION     1200
#define SONY_BIT_PERIOD               600

#define EXAMPLE_IR_SONY_DECODE_MARGIN 200  ///< Margem de erro para decodificação (us)

#ifdef __cplusplus
}
#endif

#endif // PROTOCOL_SONY_H
