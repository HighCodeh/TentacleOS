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


#ifndef IR_ENCODER_H
#define IR_ENCODER_H

#include "driver/rmt_encoder.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Protocolos IR suportados
 */
typedef enum {
    IR_PROTOCOL_NEC,
    IR_PROTOCOL_RC6,
    IR_PROTOCOL_RC5,
    IR_PROTOCOL_SAMSUNG32,
    IR_PROTOCOL_SIRC,
} ir_protocol_t;

/**
 * @brief Configuração de encoder NEC
 */
typedef struct {
    uint32_t resolution;  ///< Resolução do encoder em Hz
} ir_nec_encoder_config_t;

/**
 * @brief Configuração de encoder RC6
 */
typedef struct {
    uint32_t resolution;  ///< Resolução do encoder em Hz
} ir_rc6_encoder_config_t;

/**
 * @brief Configuração de encoder RC5
 */
typedef struct {
    uint32_t resolution;  ///< Resolução do encoder em Hz
} ir_rc5_encoder_config_t;

/**
 * @brief Configuração de encoder Samsung32
 */
typedef struct {
    uint32_t resolution;  ///< Resolução do encoder em Hz
} ir_samsung32_encoder_config_t;

/**
 * @brief Configuração de encoder Sony SIRC
 */
typedef struct {
    uint32_t resolution;  ///< Resolução do encoder em Hz
} ir_sony_encoder_config_t;

/**
 * @brief Estrutura unificada de configuração de encoder
 */
typedef struct {
    ir_protocol_t protocol;  ///< Protocolo a ser usado
    union {
        ir_nec_encoder_config_t nec;
        ir_rc6_encoder_config_t rc6;
        ir_rc5_encoder_config_t rc5;
        ir_samsung32_encoder_config_t samsung32;
        ir_sony_encoder_config_t sony;
    } config;
} ir_encoder_config_t;

/**
 * @brief Cria um novo encoder IR baseado no protocolo especificado
 *
 * @param cfg Configuração do encoder
 * @param ret_encoder Ponteiro para retornar o handle do encoder
 * @return esp_err_t ESP_OK em sucesso
 */
esp_err_t rmt_new_ir_encoder(const ir_encoder_config_t *cfg, 
                              rmt_encoder_handle_t *ret_encoder);

/**
 * @brief Converte protocolo para string
 *
 * @param protocol Protocolo
 * @return const char* Nome do protocolo
 */
const char* ir_protocol_to_string(ir_protocol_t protocol);

/**
 * @brief Converte string para protocolo
 *
 * @param protocol_str String do protocolo
 * @return ir_protocol_t Protocolo correspondente
 */
ir_protocol_t ir_string_to_protocol(const char* protocol_str);

// Declarações das funções de criação de encoders
esp_err_t rmt_new_ir_nec_encoder(const ir_nec_encoder_config_t *config,
                                  rmt_encoder_handle_t *ret_encoder);

esp_err_t rmt_new_ir_rc6_encoder(const ir_rc6_encoder_config_t *config,
                                  rmt_encoder_handle_t *ret_encoder);

esp_err_t rmt_new_ir_rc5_encoder(const ir_rc5_encoder_config_t *config,
                                  rmt_encoder_handle_t *ret_encoder);

esp_err_t rmt_new_ir_samsung32_encoder(const ir_samsung32_encoder_config_t *config,
                                        rmt_encoder_handle_t *ret_encoder);

esp_err_t rmt_new_ir_sony_encoder(const ir_sony_encoder_config_t *config,
                                   rmt_encoder_handle_t *ret_encoder);

#ifdef __cplusplus
}
#endif

#endif // IR_ENCODER_H
