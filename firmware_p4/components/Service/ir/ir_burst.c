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


#include "ir_burst.h"
#include "ir_common.h"
#include "ir_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>

static const char *TAG = "ir_burst";
static bool g_burst_running = false;
static bool g_burst_stop_requested = false;

// Configuração padrão
static const ir_burst_config_t DEFAULT_CONFIG = {
    .delay_between_files_ms = 500,
    .repeat_each_file = 1,
    .delay_between_repeats_ms = 200,
    .stop_on_error = false
};

void ir_burst_stop(void) {
    g_burst_stop_requested = true;
    ESP_LOGW(TAG, "Stop requested");
}

bool ir_burst_is_running(void) {
    return g_burst_running;
}

int ir_burst_send_all_files(const ir_burst_config_t *config) {
    if (!config) {
        config = &DEFAULT_CONFIG;
    }

    ESP_LOGI(TAG, "=== Iniciando transmissão em burst ===");
    ESP_LOGI(TAG, "Delay entre arquivos: %lu ms", config->delay_between_files_ms);
    ESP_LOGI(TAG, "Repetições por arquivo: %lu", config->repeat_each_file);
    ESP_LOGI(TAG, "Delay entre repetições: %lu ms", config->delay_between_repeats_ms);
    ESP_LOGI(TAG, "Parar em erro: %s", config->stop_on_error ? "SIM" : "NÃO");

    g_burst_running = true;
    g_burst_stop_requested = false;

    // Abre o diretório de arquivos IR
    DIR *dir = opendir(IR_STORAGE_BASE_PATH);
    if (!dir) {
        ESP_LOGE(TAG, "Falha ao abrir diretório: %s (errno=%d: %s)", 
                 IR_STORAGE_BASE_PATH, errno, strerror(errno));
        g_burst_running = false;
        return 0;
    }
    
    ESP_LOGI(TAG, "Diretório aberto com sucesso: %s", IR_STORAGE_BASE_PATH);

    // Primeira passagem: contar arquivos .ir
    struct dirent *entry;
    int total_files = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        // Ignora "." e ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        size_t len = strlen(entry->d_name);
        // Verifica extensão .ir ou .IR (FAT retorna em maiúsculas)
        if (len > 3) {
            const char *ext = entry->d_name + len - 3;
            if (strcasecmp(ext, ".ir") == 0) {
                total_files++;
            }
        }
    }
    
    ESP_LOGI(TAG, "Encontrados %d arquivos .ir", total_files);
    
    if (total_files == 0) {
        ESP_LOGW(TAG, "Nenhum arquivo .ir encontrado!");
        closedir(dir);
        g_burst_running = false;
        return 0;
    }

    // Volta ao início do diretório
    rewinddir(dir);

    int success_count = 0;
    int fail_count = 0;
    int current_file = 0;

    // Segunda passagem: transmitir arquivos
    ESP_LOGI(TAG, "Iniciando segunda passagem...");
    while ((entry = readdir(dir)) != NULL && !g_burst_stop_requested) {
        ESP_LOGD(TAG, "Lendo entrada: %s", entry->d_name);
        
        // Ignora "." e ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        size_t len = strlen(entry->d_name);
        if (len <= 3) {
            ESP_LOGD(TAG, "Ignorando (len <= 3): %s", entry->d_name);
            continue;
        }
        
        // Verifica extensão .ir ou .IR (case-insensitive)
        const char *ext = entry->d_name + len - 3;
        if (strcasecmp(ext, ".ir") != 0) {
            ESP_LOGD(TAG, "Ignorando (não .ir): %s", entry->d_name);
            continue;
        }

        current_file++;
        ESP_LOGI(TAG, "Arquivo .ir encontrado na 2ª passagem: %s", entry->d_name);

        // Remove a extensão .ir do nome
        char filename[256];
        strncpy(filename, entry->d_name, len - 3);
        filename[len - 3] = '\0';

        ESP_LOGI(TAG, "[%d/%d] Transmitindo: %s", current_file, total_files, filename);

        // Repete o arquivo N vezes
        bool file_success = true;
        for (uint32_t rep = 0; rep < config->repeat_each_file && !g_burst_stop_requested; rep++) {
            if (config->repeat_each_file > 1) {
                ESP_LOGI(TAG, "  Repetição %lu/%lu", rep + 1, config->repeat_each_file);
            }

            bool tx_result = ir_tx_send_from_file(filename);
            
            if (tx_result) {
                ESP_LOGI(TAG, "  ✅ Transmitido com sucesso");
            } else {
                ESP_LOGE(TAG, "  ❌ Falha na transmissão");
                file_success = false;
                
                if (config->stop_on_error) {
                    ESP_LOGE(TAG, "Parando devido a erro (stop_on_error=true)");
                    closedir(dir);
                    g_burst_running = false;
                    return success_count;
                }
            }

            // Delay entre repetições (exceto na última)
            if (rep < config->repeat_each_file - 1) {
                vTaskDelay(pdMS_TO_TICKS(config->delay_between_repeats_ms));
            }
        }

        if (file_success) {
            success_count++;
        } else {
            fail_count++;
        }

        // Delay entre arquivos (exceto no último)
        if (current_file < total_files && !g_burst_stop_requested) {
            vTaskDelay(pdMS_TO_TICKS(config->delay_between_files_ms));
        }
    }

    closedir(dir);

    if (g_burst_stop_requested) {
        ESP_LOGW(TAG, "=== Burst INTERROMPIDO pelo usuário ===");
    } else {
        ESP_LOGI(TAG, "=== Burst CONCLUÍDO ===");
    }
    
    ESP_LOGI(TAG, "Arquivos processados: %d", current_file);
    ESP_LOGI(TAG, "Sucessos: %d", success_count);
    ESP_LOGI(TAG, "Falhas: %d", fail_count);

    g_burst_running = false;
    return success_count;
}

int ir_burst_send_file_list(const char **filenames, uint32_t count,
                            const ir_burst_config_t *config) {
    if (!filenames || count == 0) {
        ESP_LOGE(TAG, "Lista de arquivos inválida");
        return 0;
    }

    if (!config) {
        config = &DEFAULT_CONFIG;
    }

    ESP_LOGI(TAG, "=== Transmitindo lista de %lu arquivos ===", count);

    g_burst_running = true;
    g_burst_stop_requested = false;

    int success_count = 0;
    int fail_count = 0;

    for (uint32_t i = 0; i < count && !g_burst_stop_requested; i++) {
        ESP_LOGI(TAG, "[%lu/%lu] Transmitindo: %s", i + 1, count, filenames[i]);

        bool file_success = true;
        for (uint32_t rep = 0; rep < config->repeat_each_file && !g_burst_stop_requested; rep++) {
            if (config->repeat_each_file > 1) {
                ESP_LOGI(TAG, "  Repetição %lu/%lu", rep + 1, config->repeat_each_file);
            }

            bool tx_result = ir_tx_send_from_file(filenames[i]);
            
            if (!tx_result) {
                ESP_LOGE(TAG, "  ❌ Falha na transmissão");
                file_success = false;
                
                if (config->stop_on_error) {
                    ESP_LOGE(TAG, "Parando devido a erro");
                    g_burst_running = false;
                    return success_count;
                }
            }

            if (rep < config->repeat_each_file - 1) {
                vTaskDelay(pdMS_TO_TICKS(config->delay_between_repeats_ms));
            }
        }

        if (file_success) {
            success_count++;
        } else {
            fail_count++;
        }

        if (i < count - 1 && !g_burst_stop_requested) {
            vTaskDelay(pdMS_TO_TICKS(config->delay_between_files_ms));
        }
    }

    ESP_LOGI(TAG, "=== Transmissão concluída ===");
    ESP_LOGI(TAG, "Sucessos: %d / Falhas: %d", success_count, fail_count);

    g_burst_running = false;
    return success_count;
}
