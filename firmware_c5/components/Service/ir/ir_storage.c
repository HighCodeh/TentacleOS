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


#include "ir_storage.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "IR_STORAGE";

bool ir_save(const char* protocol, uint32_t command, uint32_t address, const char* filename) {
    return ir_save_ex(protocol, command, address, 0xFF, filename);
}

bool ir_save_ex(const char* protocol, uint32_t command, uint32_t address, uint8_t toggle, const char* filename) {
    return ir_save_full(protocol, command, address, toggle, 0xFF, filename);
}

bool ir_save_full(const char* protocol, uint32_t command, uint32_t address, 
                  uint8_t toggle, uint8_t bits, const char* filename) {
    if (!protocol || !filename) {
        ESP_LOGE(TAG, "Parâmetros inválidos");
        return false;
    }
    
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/sdcard/%s.ir", filename);
    
    FILE* f = fopen(filepath, "w");
    if (!f) {
        ESP_LOGE(TAG, "Falha ao criar arquivo: %s", filepath);
        return false;
    }
    
    // Header padrão Flipper Zero
    fprintf(f, "Filetype: IR signals file\n");
    fprintf(f, "Version: 1\n");
    fprintf(f, "#\n");
    fprintf(f, "name: %s\n", filename);
    fprintf(f, "type: parsed\n");
    fprintf(f, "protocol: %s\n", protocol);
    fprintf(f, "address: %08lX\n", address);
    fprintf(f, "command: %08lX\n", command);
    
    // Adiciona toggle apenas se for válido (para RC6 e RC5)
    if (toggle != 0xFF) {
        fprintf(f, "toggle: %02X\n", toggle);
    }
    
    // Adiciona bits apenas se for válido (para Sony)
    if (bits != 0xFF) {
        fprintf(f, "bits: %d\n", bits);
    }
    
    fclose(f);
    
    // Log informativo
    if (toggle != 0xFF && bits != 0xFF) {
        ESP_LOGI(TAG, "Código IR salvo: %s -> %s (0x%08lX, 0x%08lX, toggle=%d, bits=%d)",
                 filename, protocol, address, command, toggle, bits);
    } else if (toggle != 0xFF) {
        ESP_LOGI(TAG, "Código IR salvo: %s -> %s (0x%08lX, 0x%08lX, toggle=%d)",
                 filename, protocol, address, command, toggle);
    } else if (bits != 0xFF) {
        ESP_LOGI(TAG, "Código IR salvo: %s -> %s (0x%08lX, 0x%08lX, bits=%d)",
                 filename, protocol, address, command, bits);
    } else {
        ESP_LOGI(TAG, "Código IR salvo: %s -> %s (0x%08lX, 0x%08lX)",
                 filename, protocol, address, command);
    }
    
    return true;
}

bool ir_load(const char* filename, ir_code_t* code) {
    if (!filename || !code) {
        ESP_LOGE(TAG, "Parâmetros inválidos");
        return false;
    }
    
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/sdcard/%s.ir", filename);
    
    FILE* f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGE(TAG, "Falha ao abrir arquivo: %s", filepath);
        return false;
    }
    
    char line[256];
    memset(code, 0, sizeof(ir_code_t));
    code->toggle = 0xFF;  // Valor padrão (não presente/auto)
    code->bits = 0xFF;    // Valor padrão (não presente)
    
    while (fgets(line, sizeof(line), f)) {
        // Remove newline
        line[strcspn(line, "\r\n")] = 0;
        
        if (strncmp(line, "protocol:", 9) == 0) {
            sscanf(line, "protocol: %31s", code->protocol);
        }
        else if (strncmp(line, "address:", 8) == 0) {
            sscanf(line, "address: %lX", &code->address);
        }
        else if (strncmp(line, "command:", 8) == 0) {
            sscanf(line, "command: %lX", &code->command);
        }
        else if (strncmp(line, "toggle:", 7) == 0) {
            unsigned int toggle_val;
            sscanf(line, "toggle: %X", &toggle_val);
            code->toggle = (uint8_t)toggle_val;
        }
        else if (strncmp(line, "bits:", 5) == 0) {
            unsigned int bits_val;
            sscanf(line, "bits: %u", &bits_val);
            code->bits = (uint8_t)bits_val;
        }
    }
    
    fclose(f);
    
    // Log informativo
    if (code->toggle != 0xFF && code->bits != 0xFF) {
        ESP_LOGI(TAG, "Código IR carregado: %s -> %s (0x%08lX, 0x%08lX, toggle=%d, bits=%d)",
                 filename, code->protocol, code->address, code->command, code->toggle, code->bits);
    } else if (code->toggle != 0xFF) {
        ESP_LOGI(TAG, "Código IR carregado: %s -> %s (0x%08lX, 0x%08lX, toggle=%d)",
                 filename, code->protocol, code->address, code->command, code->toggle);
    } else if (code->bits != 0xFF) {
        ESP_LOGI(TAG, "Código IR carregado: %s -> %s (0x%08lX, 0x%08lX, bits=%d)",
                 filename, code->protocol, code->address, code->command, code->bits);
    } else {
        ESP_LOGI(TAG, "Código IR carregado: %s -> %s (0x%08lX, 0x%08lX)",
                 filename, code->protocol, code->address, code->command);
    }
    
    return true;
}
