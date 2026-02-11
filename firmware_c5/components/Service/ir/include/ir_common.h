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


#ifndef IR_COMMON_H
#define IR_COMMON_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "ir_encoder.h"

// Definições comuns
#define EXAMPLE_IR_RESOLUTION_HZ     1000000 // 1MHz resolution, 1 tick = 1us
#define EXAMPLE_IR_TX_GPIO_NUM       2
#define EXAMPLE_IR_RX_GPIO_NUM       1

// Estrutura para dados compartilhados
typedef struct {
    rmt_channel_handle_t tx_channel;
    rmt_channel_handle_t rx_channel;
    rmt_encoder_handle_t encoder;
    QueueHandle_t receive_queue;
} ir_context_t;

// ========== Funções TX ==========

/**
 * @brief Inicializa módulo de transmissão IR
 */
esp_err_t ir_tx_init(ir_context_t *ctx);

/**
 * @brief Finaliza módulo de transmissão IR
 */
esp_err_t ir_tx_deinit(ir_context_t *ctx);

/**
 * @brief Envia código NEC
 */
esp_err_t ir_tx_send_nec(ir_context_t *ctx, uint16_t address, uint16_t command);

/**
 * @brief Envia código RC6
 */
esp_err_t ir_tx_send_rc6(ir_context_t *ctx, uint8_t address, uint8_t command, uint8_t toggle);

/**
 * @brief Envia código RC5
 */
esp_err_t ir_tx_send_rc5(ir_context_t *ctx, uint8_t address, uint8_t command, uint8_t toggle);

/**
 * @brief Envia código Samsung (32-bit)
 */
esp_err_t ir_tx_send_samsung32(ir_context_t *ctx, uint32_t data);

/**
 * @brief Envia código Sony SIRC (12, 15 ou 20 bits)
 */
esp_err_t ir_tx_send_sony(ir_context_t *ctx, uint16_t address, uint8_t command, uint8_t bits);

/**
 * @brief Envia código IR a partir de arquivo
 *
 * @param filename Nome do arquivo (sem extensão .ir)
 * @return true em sucesso
 */
bool ir_tx_send_from_file(const char* filename);

/**
 * @brief Reseta o estado do toggle bit do RC6
 */
void ir_tx_reset_rc6_toggle(void);

/**
 * @brief Reseta o estado do toggle bit do RC5
 */
void ir_tx_reset_rc5_toggle(void);

// ========== Funções RX ==========

/**
 * @brief Inicializa módulo de recepção IR
 */
esp_err_t ir_rx_init(ir_context_t *ctx);

/**
 * @brief Inicia recepção de dados IR
 */
void ir_rx_start_receive(ir_context_t *ctx);

/**
 * @brief Aguarda dados recebidos
 */
bool ir_rx_wait_for_data(ir_context_t *ctx, rmt_rx_done_event_data_t *rx_data, uint32_t timeout_ms);

/**
 * @brief Recebe e salva sinal IR em arquivo
 *
 * @param filename Nome do arquivo (sem extensão)
 * @param timeout_ms Timeout em milissegundos
 * @return true em sucesso
 */
bool ir_receive(const char* filename, uint32_t timeout_ms);

// ========== Parsers ==========

/**
 * @brief Parse de frame NEC
 */
void parse_nec_frame(rmt_symbol_word_t *symbols, size_t num_symbols, const char* filename);

/**
 * @brief Parse de frame RC6
 */
void parse_rc6_frame(rmt_symbol_word_t *symbols, size_t num_symbols, const char* filename);

#endif // IR_COMMON_H
