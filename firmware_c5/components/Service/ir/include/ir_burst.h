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


#ifndef IR_BURST_H
#define IR_BURST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuração de burst IR
 */
typedef struct {
    uint32_t delay_between_files_ms;  ///< Delay entre cada arquivo (ms)
    uint32_t repeat_each_file;        ///< Quantas vezes repetir cada arquivo
    uint32_t delay_between_repeats_ms;///< Delay entre repetições do mesmo arquivo (ms)
    bool stop_on_error;               ///< Parar se houver erro (false = continuar)
} ir_burst_config_t;

/**
 * @brief Transmite todos os arquivos .ir encontrados no cartão SD
 *
 * @param config Configuração do burst (NULL = usa padrões)
 * @return int Número de arquivos transmitidos com sucesso
 */
int ir_burst_send_all_files(const ir_burst_config_t *config);

/**
 * @brief Transmite lista específica de arquivos
 *
 * @param filenames Array de nomes de arquivos (sem extensão .ir)
 * @param count Número de arquivos no array
 * @param config Configuração do burst (NULL = usa padrões)
 * @return int Número de arquivos transmitidos com sucesso
 */
int ir_burst_send_file_list(const char **filenames, uint32_t count,
                            const ir_burst_config_t *config);

/**
 * @brief Para transmissão em progresso (se estiver rodando em outra task)
 */
void ir_burst_stop(void);

/**
 * @brief Verifica se burst está em execução
 *
 * @return true se burst está ativo
 */
bool ir_burst_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // IR_BURST_H
