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

static const char *TAG = "RX";

// Buffer estático para recepção
static rmt_symbol_word_t raw_symbols[64];

static bool ir_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

esp_err_t ir_rx_init(ir_context_t *ctx)
{
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "create RMT RX channel");
    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = EXAMPLE_IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .gpio_num = EXAMPLE_IR_RX_GPIO_NUM,
    };
    
    ret = rmt_new_rx_channel(&rx_channel_cfg, &ctx->rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "register RX done callback");
    ctx->receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (!ctx->receive_queue) {
        ESP_LOGE(TAG, "Failed to create receive queue");
        return ESP_ERR_NO_MEM;
    }
    
    rmt_rx_event_callbacks_t cbs = { 
        .on_recv_done = ir_rx_done_callback 
    };
    ret = rmt_rx_register_event_callbacks(ctx->rx_channel, &cbs, ctx->receive_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register callbacks: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "enable RMT RX channel");
    ret = rmt_enable(ctx->rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RX module initialized successfully");
    return ret;
}

esp_err_t ir_rx_deinit(ir_context_t *ctx)
{
    if (!ctx) {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;

    // Desabilitar canal RX se existe
    if (ctx->rx_channel) {
        ESP_LOGI(TAG, "Disabling RMT RX channel");
        esp_err_t disable_ret = rmt_disable(ctx->rx_channel);
        if (disable_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable RX channel: %s", esp_err_to_name(disable_ret));
            ret = disable_ret;
        }

        ESP_LOGI(TAG, "Deleting RMT RX channel");
        esp_err_t del_ret = rmt_del_channel(ctx->rx_channel);
        if (del_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete RX channel: %s", esp_err_to_name(del_ret));
            ret = del_ret;
        }
        ctx->rx_channel = NULL;
    }

    // Deletar queue se existe
    if (ctx->receive_queue) {
        ESP_LOGI(TAG, "Deleting receive queue");
        vQueueDelete(ctx->receive_queue);
        ctx->receive_queue = NULL;
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RX module deinitialized successfully");
    }

    return ret;
}

void ir_rx_start_receive(ir_context_t *ctx)
{
    if (!ctx || !ctx->rx_channel) {
        ESP_LOGE(TAG, "RX not initialized");
        return;
    }

    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };
    
    esp_err_t ret = rmt_receive(ctx->rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start receive: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Started IR receive operation");
    }
}

bool ir_rx_wait_for_data(ir_context_t *ctx, rmt_rx_done_event_data_t *rx_data, uint32_t timeout_ms)
{
    if (!ctx || !ctx->receive_queue || !rx_data) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    //Protocols

    

    if (xQueueReceive(ctx->receive_queue, rx_data, pdMS_TO_TICKS(timeout_ms)) == pdPASS) {
        ESP_LOGI(TAG, "Received IR data: %d symbols", rx_data->num_symbols);
        return true;
    }
    
    return false;  // timeout
}

//FUNÇÃO PRINCIPAL
bool ir_receive(const char* filename, uint32_t timeout_ms) {

    if (!filename) {
        ESP_LOGE(TAG, "Nome do arquivo inválido");
        return false;
    }
    
    ir_context_t ir_ctx = {0};

    esp_err_t ret = ir_rx_init(&ir_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RX: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Aguardando sinal IR... (timeout: %lu ms)", timeout_ms);
    
    ir_rx_start_receive(&ir_ctx);
    rmt_rx_done_event_data_t rx_data;
    bool success = false;

    if (ir_rx_wait_for_data(&ir_ctx, &rx_data, timeout_ms)) {
        parse_nec_frame(rx_data.received_symbols, rx_data.num_symbols, filename);
        success = true;
    } else {
        ESP_LOGW(TAG, "Timeout - nenhum sinal recebido");
    }

    // Desalocar canal RX
    ir_rx_deinit(&ir_ctx);
    
    return success;
}
