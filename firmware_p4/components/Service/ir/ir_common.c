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


#include "ir_common.h"
#include "ir_storage.h"
#include "protocol_nec.h"
#include "protocol_rc6.h"
#include "protocol_rc5.h"
#include "protocol_samsung32.h"
#include "protocol_sony.h"
#include <string.h>

static const char *TAG = "ir_tx";

// Estados globais de toggle
static uint8_t g_rc6_toggle_state = 0;
static uint8_t g_rc5_toggle_state = 0;

bool ir_tx_send_from_file(const char* filename) {
    ESP_LOGI(TAG, "=== INÍCIO ir_tx_send_from_file ===");
    ESP_LOGI(TAG, "Filename: %s", filename);
    
    ir_context_t ir_ctx = {0};
    
    // Carrega o código do arquivo
    ir_code_t ir_code;
    ESP_LOGI(TAG, "Carregando arquivo...");
    if (!ir_load(filename, &ir_code)) {
        ESP_LOGE(TAG, "❌ Falha ao carregar código do arquivo: %s", filename);
        return false;
    }

    ESP_LOGI(TAG, "✅ Arquivo carregado:");
    ESP_LOGI(TAG, "  Protocol: %s", ir_code.protocol);
    ESP_LOGI(TAG, "  Address: 0x%08lX", ir_code.address);
    ESP_LOGI(TAG, "  Command: 0x%08lX", ir_code.command);
    ESP_LOGI(TAG, "  Toggle: %d (0xFF=auto)", ir_code.toggle);
    if (ir_code.bits != 0xFF) {
        ESP_LOGI(TAG, "  Bits: %d", ir_code.bits);
    }

    // Inicializa TX
    ESP_LOGI(TAG, "Inicializando TX...");
    esp_err_t ret = ir_tx_init(&ir_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize TX: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "✅ TX inicializado");

    // Cria encoder apropriado baseado no protocolo
    ir_encoder_config_t enc_cfg = {0};
    
    ESP_LOGI(TAG, "Identificando protocolo: %s", ir_code.protocol);
    
    if (strcmp(ir_code.protocol, "NEC") == 0) {
        ESP_LOGI(TAG, "Configurando encoder NEC");
        enc_cfg.protocol = IR_PROTOCOL_NEC;
        enc_cfg.config.nec.resolution = EXAMPLE_IR_RESOLUTION_HZ;
    }
    else if (strcmp(ir_code.protocol, "RC6") == 0) {
        ESP_LOGI(TAG, "Configurando encoder RC6");
        enc_cfg.protocol = IR_PROTOCOL_RC6;
        enc_cfg.config.rc6.resolution = EXAMPLE_IR_RESOLUTION_HZ;
    }
    else if (strcmp(ir_code.protocol, "RC5") == 0) {
        ESP_LOGI(TAG, "Configurando encoder RC5");
        enc_cfg.protocol = IR_PROTOCOL_RC5;
        enc_cfg.config.rc5.resolution = EXAMPLE_IR_RESOLUTION_HZ;
    }
    else if (strcmp(ir_code.protocol, "Samsung32") == 0) {
        ESP_LOGI(TAG, "Configurando encoder Samsung32");
        enc_cfg.protocol = IR_PROTOCOL_SAMSUNG32;
        enc_cfg.config.samsung32.resolution = EXAMPLE_IR_RESOLUTION_HZ;
    }
    else if (strcmp(ir_code.protocol, "SIRC") == 0 || strcmp(ir_code.protocol, "Sony") == 0) {
        ESP_LOGI(TAG, "Configurando encoder Sony SIRC");
        enc_cfg.protocol = IR_PROTOCOL_SIRC;
        enc_cfg.config.sony.resolution = EXAMPLE_IR_RESOLUTION_HZ;
    }
    else {
        ESP_LOGE(TAG, "❌ Protocolo não suportado: %s", ir_code.protocol);
        ir_tx_deinit(&ir_ctx);
        return false;
    }

    ESP_LOGI(TAG, "Criando encoder (protocol=%d)...", enc_cfg.protocol);
    ret = rmt_new_ir_encoder(&enc_cfg, &ir_ctx.encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to create encoder: %s", esp_err_to_name(ret));
        ir_tx_deinit(&ir_ctx);
        return false;
    }
    ESP_LOGI(TAG, "✅ Encoder criado");

    // Transmite baseado no protocolo
    ret = ESP_FAIL;
    
    if (strcmp(ir_code.protocol, "NEC") == 0) {
        ESP_LOGI(TAG, "Transmitindo NEC: addr=0x%04X, cmd=0x%04X", 
                 (uint16_t)ir_code.address, (uint16_t)ir_code.command);
        ret = ir_tx_send_nec(&ir_ctx, (uint16_t)ir_code.address, (uint16_t)ir_code.command);
    }
    else if (strcmp(ir_code.protocol, "RC6") == 0) {
        uint8_t toggle = (ir_code.toggle != 0xFF) ? ir_code.toggle : g_rc6_toggle_state;
        ESP_LOGI(TAG, "Transmitindo RC6: addr=0x%02X, cmd=0x%02X, toggle=%d", 
                 (uint8_t)ir_code.address, (uint8_t)ir_code.command, toggle);
        ret = ir_tx_send_rc6(&ir_ctx, (uint8_t)ir_code.address, (uint8_t)ir_code.command, toggle);
        
        if (ir_code.toggle == 0xFF) {
            g_rc6_toggle_state = !g_rc6_toggle_state;
            ESP_LOGD(TAG, "Toggle RC6 alternado para: %d", g_rc6_toggle_state);
        }
    }
    else if (strcmp(ir_code.protocol, "RC5") == 0) {
        uint8_t toggle = (ir_code.toggle != 0xFF) ? ir_code.toggle : g_rc5_toggle_state;
        ESP_LOGI(TAG, "Transmitindo RC5: addr=0x%02X, cmd=0x%02X, toggle=%d", 
                 (uint8_t)ir_code.address, (uint8_t)ir_code.command, toggle);
        ret = ir_tx_send_rc5(&ir_ctx, (uint8_t)ir_code.address, (uint8_t)ir_code.command, toggle);
        
        if (ir_code.toggle == 0xFF) {
            g_rc5_toggle_state = !g_rc5_toggle_state;
            ESP_LOGD(TAG, "Toggle RC5 alternado para: %d", g_rc5_toggle_state);
        }
    }
    else if (strcmp(ir_code.protocol, "Samsung32") == 0) {
        // Samsung32 usa os 32 bits completos (address e command combinados)
        uint32_t data = ((ir_code.address & 0xFFFF) << 16) | (ir_code.command & 0xFFFF);
        ESP_LOGI(TAG, "Transmitindo Samsung32: data=0x%08lX", data);
        ret = ir_tx_send_samsung32(&ir_ctx, data);
    }
    else if (strcmp(ir_code.protocol, "SIRC") == 0 || strcmp(ir_code.protocol, "Sony") == 0) {
        uint8_t bits = (ir_code.bits != 0xFF) ? ir_code.bits : 12;  // Default 12 bits
        ESP_LOGI(TAG, "Transmitindo Sony SIRC-%d: addr=0x%04X, cmd=0x%02X", 
                 bits, (uint16_t)ir_code.address, (uint8_t)ir_code.command);
        ret = ir_tx_send_sony(&ir_ctx, (uint16_t)ir_code.address, (uint8_t)ir_code.command, bits);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Falha ao transmitir código IR: %s", esp_err_to_name(ret));
        ir_tx_deinit(&ir_ctx);
        return false;
    }

    ESP_LOGI(TAG, "✅ Código IR transmitido com sucesso!");

    vTaskDelay(pdMS_TO_TICKS(100));
    ir_tx_deinit(&ir_ctx);

    ESP_LOGI(TAG, "=== FIM ir_tx_send_from_file ===\n");
    return true;
}

// ========== Funções de inicialização ==========

esp_err_t ir_tx_init(ir_context_t *ctx)
{
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "create RMT TX channel");
    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = EXAMPLE_IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = EXAMPLE_IR_TX_GPIO_NUM,
    };
    
    ret = rmt_new_tx_channel(&tx_channel_cfg, &ctx->tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create TX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "modulate carrier to TX channel");
    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = 38000,
    };
    ret = rmt_apply_carrier(ctx->tx_channel, &carrier_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply carrier: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "enable RMT TX channel");
    ret = rmt_enable(ctx->tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable TX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TX module initialized successfully");
    return ret;
}

esp_err_t ir_tx_deinit(ir_context_t *ctx)
{
    if (!ctx) {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;

    if (ctx->encoder) {
        ESP_LOGI(TAG, "Deleting IR encoder");
        esp_err_t del_enc_ret = rmt_del_encoder(ctx->encoder);
        if (del_enc_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete encoder: %s", esp_err_to_name(del_enc_ret));
            ret = del_enc_ret;
        }
        ctx->encoder = NULL;
    }

    if (ctx->tx_channel) {
        ESP_LOGI(TAG, "Disabling RMT TX channel");
        esp_err_t disable_ret = rmt_disable(ctx->tx_channel);
        if (disable_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable TX channel: %s", esp_err_to_name(disable_ret));
            ret = disable_ret;
        }

        ESP_LOGI(TAG, "Deleting RMT TX channel");
        esp_err_t del_ret = rmt_del_channel(ctx->tx_channel);
        if (del_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete TX channel: %s", esp_err_to_name(del_ret));
            ret = del_ret;
        }
        ctx->tx_channel = NULL;
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "TX module deinitialized successfully");
    }

    return ret;
}

// ========== Funções de transmissão específicas ==========

esp_err_t ir_tx_send_nec(ir_context_t *ctx, uint16_t address, uint16_t command)
{
    if (!ctx || !ctx->tx_channel || !ctx->encoder) {
        ESP_LOGE(TAG, "TX not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const ir_nec_scan_code_t scan_code = {
        .address = address,
        .command = command,
    };
    
    rmt_transmit_config_t transmit_config = { 
        .loop_count = 0 
    };
    
    esp_err_t ret = rmt_transmit(ctx->tx_channel, ctx->encoder, &scan_code, sizeof(scan_code), &transmit_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sent NEC code - Address: 0x%04X, Command: 0x%04X", address, command);
    }
    
    return ret;
}

esp_err_t ir_tx_send_rc6(ir_context_t *ctx, uint8_t address, uint8_t command, uint8_t toggle)
{
    if (!ctx || !ctx->tx_channel || !ctx->encoder) {
        ESP_LOGE(TAG, "TX not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const ir_rc6_scan_code_t scan_code = {
        .address = address,
        .command = command,
        .toggle = toggle & 0x1,
    };
    
    rmt_transmit_config_t transmit_config = { 
        .loop_count = 0 
    };
    
    esp_err_t ret = rmt_transmit(ctx->tx_channel, ctx->encoder, &scan_code, sizeof(scan_code), &transmit_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sent RC6 code - Address: 0x%02X, Command: 0x%02X, Toggle: %d", 
                 address, command, toggle);
    }
    
    return ret;
}

esp_err_t ir_tx_send_rc5(ir_context_t *ctx, uint8_t address, uint8_t command, uint8_t toggle)
{
    if (!ctx || !ctx->tx_channel || !ctx->encoder) {
        ESP_LOGE(TAG, "TX not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const ir_rc5_scan_code_t scan_code = {
        .address = address & 0x1F,  // 5 bits
        .command = command & 0x3F,  // 6 bits
        .toggle = toggle & 0x1,
    };
    
    rmt_transmit_config_t transmit_config = { 
        .loop_count = 0 
    };
    
    esp_err_t ret = rmt_transmit(ctx->tx_channel, ctx->encoder, &scan_code, sizeof(scan_code), &transmit_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sent RC5 code - Address: 0x%02X, Command: 0x%02X, Toggle: %d", 
                 address, command, toggle);
    }
    
    return ret;
}

esp_err_t ir_tx_send_samsung32(ir_context_t *ctx, uint32_t data)
{
    if (!ctx || !ctx->tx_channel || !ctx->encoder) {
        ESP_LOGE(TAG, "TX not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const ir_samsung32_scan_code_t scan_code = {
        .data = data,
    };
    
    rmt_transmit_config_t transmit_config = { 
        .loop_count = 0 
    };
    
    esp_err_t ret = rmt_transmit(ctx->tx_channel, ctx->encoder, &scan_code, sizeof(scan_code), &transmit_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sent Samsung32 code - Data: 0x%08lX", data);
    }
    
    return ret;
}

esp_err_t ir_tx_send_sony(ir_context_t *ctx, uint16_t address, uint8_t command, uint8_t bits)
{
    if (!ctx || !ctx->tx_channel || !ctx->encoder) {
        ESP_LOGE(TAG, "TX not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Valida número de bits
    if (bits != 12 && bits != 15 && bits != 20) {
        ESP_LOGE(TAG, "Invalid bit count: %d (must be 12, 15, or 20)", bits);
        return ESP_ERR_INVALID_ARG;
    }

    const ir_sony_scan_code_t scan_code = {
        .address = address,
        .command = command & 0x7F,  // 7 bits
        .bits = bits,
    };
    
    rmt_transmit_config_t transmit_config = { 
        .loop_count = 0 
    };
    
    esp_err_t ret = rmt_transmit(ctx->tx_channel, ctx->encoder, &scan_code, sizeof(scan_code), &transmit_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Sent Sony SIRC-%d code - Address: 0x%04X, Command: 0x%02X", 
                 bits, address, command);
    }
    
    return ret;
}

void ir_tx_reset_rc6_toggle(void) {
    g_rc6_toggle_state = 0;
    ESP_LOGI(TAG, "RC6 toggle state reset to 0");
}

void ir_tx_reset_rc5_toggle(void) {
    g_rc5_toggle_state = 0;
    ESP_LOGI(TAG, "RC5 toggle state reset to 0");
}
