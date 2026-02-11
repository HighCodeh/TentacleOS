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


#include "ir_samsung_tv.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "IR_SAMSUNG";

// Handles globais
static rmt_channel_handle_t tx_channel = NULL;
static rmt_channel_handle_t rx_channel = NULL;
static rmt_encoder_handle_t ir_encoder = NULL;  // MUDANÇA: encoder com portadora

// Fila de eventos RX
static QueueHandle_t rx_queue = NULL;

// Buffer estático para RX contínuo
static rmt_symbol_word_t s_rx_cont_buf[512];

// --- NOVO: Encoder IR com portadora de 38kHz ---
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *copy_encoder;
    rmt_encoder_t *bytes_encoder;
    rmt_symbol_word_t samsung_header;
    rmt_symbol_word_t samsung_bit_one;
    rmt_symbol_word_t samsung_bit_zero;
    rmt_symbol_word_t samsung_end;
    int state;
} rmt_ir_samsung_encoder_t;

static size_t rmt_encode_ir_samsung(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                     const void *primary_data, size_t data_size,
                                     rmt_encode_state_t *ret_state)
{
    rmt_ir_samsung_encoder_t *samsung_encoder = __containerof(encoder, rmt_ir_samsung_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    rmt_encoder_handle_t copy_encoder = samsung_encoder->copy_encoder;
    rmt_encoder_handle_t bytes_encoder = samsung_encoder->bytes_encoder;

    uint32_t *data = (uint32_t *)primary_data;

    switch (samsung_encoder->state) {
    case 0: // Header
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, 
                                                &samsung_encoder->samsung_header, 
                                                sizeof(rmt_symbol_word_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            samsung_encoder->state = 1;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    case 1: // 32 bits de dados
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, data, 4, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            samsung_encoder->state = 2;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    case 2: // End bit
        encoded_symbols += copy_encoder->encode(copy_encoder, channel,
                                                &samsung_encoder->samsung_end,
                                                sizeof(rmt_symbol_word_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            samsung_encoder->state = RMT_ENCODING_RESET;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_ir_samsung_encoder(rmt_encoder_t *encoder)
{
    rmt_ir_samsung_encoder_t *samsung_encoder = __containerof(encoder, rmt_ir_samsung_encoder_t, base);
    rmt_del_encoder(samsung_encoder->copy_encoder);
    rmt_del_encoder(samsung_encoder->bytes_encoder);
    free(samsung_encoder);
    return ESP_OK;
}

static esp_err_t rmt_ir_samsung_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_ir_samsung_encoder_t *samsung_encoder = __containerof(encoder, rmt_ir_samsung_encoder_t, base);
    rmt_encoder_reset(samsung_encoder->copy_encoder);
    rmt_encoder_reset(samsung_encoder->bytes_encoder);
    samsung_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

static esp_err_t rmt_new_ir_samsung_encoder(rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_ir_samsung_encoder_t *samsung_encoder = calloc(1, sizeof(rmt_ir_samsung_encoder_t));
    if (!samsung_encoder) return ESP_ERR_NO_MEM;

    samsung_encoder->base.encode = rmt_encode_ir_samsung;
    samsung_encoder->base.del = rmt_del_ir_samsung_encoder;
    samsung_encoder->base.reset = rmt_ir_samsung_encoder_reset;

    // Copy encoder
    rmt_copy_encoder_config_t copy_config = {};
    ret = rmt_new_copy_encoder(&copy_config, &samsung_encoder->copy_encoder);
    if (ret != ESP_OK) goto err;

    // Bytes encoder com portadora de 38kHz
    rmt_bytes_encoder_config_t bytes_config = {
        .bit0 = {
            .duration0 = SAMSUNG_BIT_ZERO_HIGH_US,
            .level0 = 1,
            .duration1 = SAMSUNG_BIT_ZERO_LOW_US,
            .level1 = 0,
        },
        .bit1 = {
            .duration0 = SAMSUNG_BIT_ONE_HIGH_US,
            .level0 = 1,
            .duration1 = SAMSUNG_BIT_ONE_LOW_US,
            .level1 = 0,
        },
        .flags.msb_first = 1  // MSB primeiro
    };
    ret = rmt_new_bytes_encoder(&bytes_config, &samsung_encoder->bytes_encoder);
    if (ret != ESP_OK) goto err;

    // Símbolos Samsung
    samsung_encoder->samsung_header = (rmt_symbol_word_t){
        .duration0 = SAMSUNG_HEADER_HIGH_US,
        .level0 = 1,
        .duration1 = SAMSUNG_HEADER_LOW_US,
        .level1 = 0,
    };
    samsung_encoder->samsung_end = (rmt_symbol_word_t){
        .duration0 = SAMSUNG_END_HIGH_US,
        .level0 = 1,
        .duration1 = 0,
        .level1 = 0,
    };

    *ret_encoder = &samsung_encoder->base;
    return ESP_OK;

err:
    if (samsung_encoder->copy_encoder) rmt_del_encoder(samsung_encoder->copy_encoder);
    if (samsung_encoder->bytes_encoder) rmt_del_encoder(samsung_encoder->bytes_encoder);
    free(samsung_encoder);
    return ret;
}

// --- Callback RX ---
static bool rx_done_isr_cb(rmt_channel_handle_t channel,
                           const rmt_rx_done_event_data_t *edata,
                           void *user_ctx)
{
    BaseType_t hpw = pdFALSE;
    QueueHandle_t q = (QueueHandle_t)user_ctx;
    xQueueSendFromISR(q, edata, &hpw);
    return hpw == pdTRUE;
}

esp_err_t ir_samsung_init(gpio_num_t tx_gpio, gpio_num_t rx_gpio, bool invert_rx)
{
    // Fila de RX
    if (!rx_queue) {
        rx_queue = xQueueCreate(8, sizeof(rmt_rx_done_event_data_t));
        if (!rx_queue) {
            ESP_LOGE(TAG, "xQueueCreate failed");
            return ESP_ERR_NO_MEM;
        }
    }

    // --- TX channel com PORTADORA 38kHz ---
    if (!tx_channel) {
        rmt_tx_channel_config_t tx_cfg = {
            .gpio_num = tx_gpio,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000,  // 1MHz = 1us
            .mem_block_symbols = 64,
            .trans_queue_depth = 4,
            .intr_priority = 0,
            .flags.invert_out = false,
            .flags.with_dma = false,
        };
        esp_err_t err = rmt_new_tx_channel(&tx_cfg, &tx_channel);
        if (err != ESP_OK) { 
            ESP_LOGE(TAG, "new tx: %s", esp_err_to_name(err)); 
            return err; 
        }

        // CRÍTICO: Configura portadora de 38kHz
        rmt_carrier_config_t carrier_cfg = {
            .frequency_hz = 38000,  // 38kHz
            .duty_cycle = 0.33,     // 33% duty cycle
            .flags.polarity_active_low = false,
        };
        err = rmt_apply_carrier(tx_channel, &carrier_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "carrier config: %s", esp_err_to_name(err));
            return err;
        }

        err = rmt_enable(tx_channel);
        if (err != ESP_OK) { 
            ESP_LOGE(TAG, "enable tx: %s", esp_err_to_name(err)); 
            return err; 
        }

        // Cria encoder Samsung customizado
        err = rmt_new_ir_samsung_encoder(&ir_encoder);
        if (err != ESP_OK) { 
            ESP_LOGE(TAG, "samsung encoder: %s", esp_err_to_name(err)); 
            return err; 
        }
    }

    // --- RX channel ---
    if (!rx_channel) {
        rmt_rx_channel_config_t rx_cfg = {
            .gpio_num = rx_gpio,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000,
            .mem_block_symbols = 128,
            .intr_priority = 0,
            .flags.invert_in = invert_rx,
            .flags.with_dma = false,
        };
        esp_err_t err = rmt_new_rx_channel(&rx_cfg, &rx_channel);
        if (err != ESP_OK) { 
            ESP_LOGE(TAG, "new rx: %s", esp_err_to_name(err)); 
            return err; 
        }

        rmt_rx_event_callbacks_t cbs = {.on_recv_done = rx_done_isr_cb};
        err = rmt_rx_register_event_callbacks(rx_channel, &cbs, rx_queue);
        if (err != ESP_OK) { 
            ESP_LOGE(TAG, "rx cb: %s", esp_err_to_name(err)); 
            return err; 
        }

        err = rmt_enable(rx_channel);
        if (err != ESP_OK) { 
            ESP_LOGE(TAG, "enable rx: %s", esp_err_to_name(err)); 
            return err; 
        }
    }

    ESP_LOGI(TAG, "Samsung IR initialized - TX: GPIO%d (38kHz carrier), RX: GPIO%d", 
             tx_gpio, rx_gpio);
    return ESP_OK;
}

esp_err_t ir_samsung_send(uint32_t data)
{
    if (!tx_channel || !ir_encoder) {
        ESP_LOGE(TAG, "not init");
        return ESP_ERR_INVALID_STATE;
    }

    // Inverte os bytes para enviar MSB primeiro
    uint32_t data_swapped = __builtin_bswap32(data);

    rmt_transmit_config_t txcfg = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    
    esp_err_t err = rmt_transmit(tx_channel, ir_encoder, &data_swapped, 4, &txcfg);
    if (err != ESP_OK) { 
        ESP_LOGE(TAG, "tx: %s", esp_err_to_name(err)); 
        return err; 
    }

    err = rmt_tx_wait_all_done(tx_channel, 1000);
    if (err != ESP_OK) { 
        ESP_LOGE(TAG, "tx wait: %s", esp_err_to_name(err)); 
        return err; 
    }

    ESP_LOGI(TAG, "Sent Samsung: 0x%08" PRIX32, data);
    return ESP_OK;
}

// --- Decodificação Samsung ---
static bool decode_samsung32(const rmt_symbol_word_t *sym, int count, samsung_ir_data_t *out)
{
    int idx = -1;
    for (int i = 0; i < count && i < 4; i++) {
        int h = sym[i].duration0;
        int l = sym[i].duration1;
        if (h > 3500 && h < 5500 && l > 3500 && l < 5500) { 
            idx = i; 
            break; 
        }
    }
    if (idx < 0) return false;

    if ((count - (idx + 1)) < 32) return false;

    uint32_t bits = 0;
    for (int b = 0; b < 32; b++) {
        const rmt_symbol_word_t s = sym[idx + 1 + b];
        int total = s.duration0 + s.duration1;
        bits <<= 1;
        bits |= (total > 1500) ? 1U : 0U;
    }

    out->raw_data = bits;
    out->address  = (bits >> 16) & 0xFFFF;
    out->command  =  bits        & 0xFFFF;
    return true;
}

esp_err_t ir_samsung_receive(samsung_ir_data_t* out, uint32_t timeout_ms)
{
    if (!rx_channel || !rx_queue || !out) {
        ESP_LOGE(TAG, "not init/args");
        return ESP_ERR_INVALID_STATE;
    }

    rmt_symbol_word_t stack_buf[200];
    rmt_receive_config_t rxcfg = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };

    esp_err_t err = rmt_receive(rx_channel, stack_buf, sizeof(stack_buf), &rxcfg);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "RX not enabled, enabling...");
        err = rmt_enable(rx_channel);
        if (err != ESP_OK) { 
            ESP_LOGE(TAG, "enable rx: %s", esp_err_to_name(err)); 
            return err; 
        }
        err = rmt_receive(rx_channel, stack_buf, sizeof(stack_buf), &rxcfg);
    }
    if (err != ESP_OK) { 
        ESP_LOGE(TAG, "rmt_receive: %s", esp_err_to_name(err)); 
        return err; 
    }

    rmt_rx_done_event_data_t ev;
    if (xQueueReceive(rx_queue, &ev, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "Receive timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (ev.num_symbols < 34) {
        ESP_LOGW(TAG, "Few symbols: %d", ev.num_symbols);
        return ESP_ERR_INVALID_SIZE;
    }

    if (!decode_samsung32(ev.received_symbols, ev.num_symbols, out)) {
        ESP_LOGW(TAG, "Decode failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "RX OK: raw=0x%08" PRIX32 " addr=0x%04" PRIX32 " cmd=0x%04" PRIX32,
             out->raw_data, (uint32_t)out->address, (uint32_t)out->command);
    return ESP_OK;
}

esp_err_t ir_samsung_start_continuous_receive(void)
{
    if (!rx_channel || !rx_queue) return ESP_ERR_INVALID_STATE;

    rmt_receive_config_t rxcfg = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };

    esp_err_t err = rmt_receive(rx_channel, s_rx_cont_buf, sizeof(s_rx_cont_buf), &rxcfg);
    if (err == ESP_ERR_INVALID_STATE) {
        err = rmt_enable(rx_channel);
        if (err != ESP_OK) { 
            ESP_LOGE(TAG, "enable rx: %s", esp_err_to_name(err)); 
            return err; 
        }
        err = rmt_receive(rx_channel, s_rx_cont_buf, sizeof(s_rx_cont_buf), &rxcfg);
    }
    if (err != ESP_OK) { 
        ESP_LOGE(TAG, "rmt_receive(cont): %s", esp_err_to_name(err)); 
    }
    return err;
}

esp_err_t ir_samsung_poll_last(samsung_ir_data_t* out, uint32_t timeout_ms)
{
    if (!rx_channel || !rx_queue || !out) return ESP_ERR_INVALID_STATE;

    rmt_rx_done_event_data_t ev;
    if (xQueueReceive(rx_queue, &ev, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    rmt_receive_config_t rxcfg = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };
    esp_err_t err = rmt_receive(rx_channel, s_rx_cont_buf, sizeof(s_rx_cont_buf), &rxcfg);
    if (err == ESP_ERR_INVALID_STATE) {
        err = rmt_enable(rx_channel);
        if (err == ESP_OK) {
            err = rmt_receive(rx_channel, s_rx_cont_buf, sizeof(s_rx_cont_buf), &rxcfg);
        }
    }
    if (err != ESP_OK) { 
        ESP_LOGE(TAG, "re-arm failed: %s", esp_err_to_name(err)); 
    }

    if (!decode_samsung32(ev.received_symbols, ev.num_symbols, out)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void ir_samsung_deinit(void)
{
    if (ir_encoder) { 
        rmt_del_encoder(ir_encoder); 
        ir_encoder = NULL; 
    }

    if (tx_channel) {
        rmt_disable(tx_channel);
        rmt_del_channel(tx_channel);
        tx_channel = NULL;
    }
    if (rx_channel) {
        rmt_disable(rx_channel);
        rmt_del_channel(rx_channel);
        rx_channel = NULL;
    }
    if (rx_queue) {
        vQueueDelete(rx_queue);
        rx_queue = NULL;
    }
    ESP_LOGI(TAG, "Samsung IR deinitialized");
}
