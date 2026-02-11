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


#include "ir_encoder.h"
#include "protocol_sony.h"
#include "esp_check.h"
#include <string.h>

static const char *TAG = "sony_encoder";

// Timings Sony SIRC em microsegundos
#define SONY_HEADER_MARK    2400
#define SONY_ONE_MARK       1200
#define SONY_ZERO_MARK       600
#define SONY_SPACE           600

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *copy_encoder;
    rmt_symbol_word_t *symbols;
    size_t num_symbols;
    size_t current_symbol;
    uint32_t resolution;
    int state;
} rmt_ir_sony_encoder_t;

static inline uint32_t us_to_ticks(uint32_t us, uint32_t resolution) {
    return (us * resolution) / 1000000;
}

static void build_sony_symbols(rmt_ir_sony_encoder_t *encoder, const ir_sony_scan_code_t *scan_code) {
    uint32_t res = encoder->resolution;
    size_t idx = 0;
    
    // Construir frame (7 command bits + address bits)
    // Sony usa LSB first
    uint32_t frame = 0;
    frame |= (scan_code->command & 0x7F);  // 7 bits de comando
    frame |= ((uint32_t)scan_code->address << 7);
    
    ESP_LOGI(TAG, "Building Sony-%d frame: 0x%05lX (addr=0x%04X, cmd=0x%02X)",
             scan_code->bits, frame, scan_code->address, scan_code->command);
    
    // Aloca símbolos (header + bits * 2 + margem)
    encoder->symbols = malloc(sizeof(rmt_symbol_word_t) * 50);
    if (!encoder->symbols) {
        ESP_LOGE(TAG, "Failed to allocate symbols");
        return;
    }
    
    // Header (start bit)
    encoder->symbols[idx++] = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = us_to_ticks(SONY_HEADER_MARK, res),
        .level1 = 0,
        .duration1 = us_to_ticks(SONY_SPACE, res)
    };
    
    // Data bits (LSB first) com pulse width encoding
    for (uint8_t i = 0; i < scan_code->bits; i++) {
        bool bit = (frame >> i) & 1;
        
        if (bit) {
            // Bit 1: long mark (1200us) + space (600us)
            encoder->symbols[idx++] = (rmt_symbol_word_t) {
                .level0 = 1,
                .duration0 = us_to_ticks(SONY_ONE_MARK, res),
                .level1 = 0,
                .duration1 = us_to_ticks(SONY_SPACE, res)
            };
        } else {
            // Bit 0: short mark (600us) + space (600us)
            encoder->symbols[idx++] = (rmt_symbol_word_t) {
                .level0 = 1,
                .duration0 = us_to_ticks(SONY_ZERO_MARK, res),
                .level1 = 0,
                .duration1 = us_to_ticks(SONY_SPACE, res)
            };
        }
    }
    
    // Ending space
    encoder->symbols[idx++] = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = us_to_ticks(SONY_SPACE, res),
        .level1 = 0,
        .duration1 = 0
    };
    
    encoder->num_symbols = idx;
    ESP_LOGI(TAG, "Built %zu symbols", encoder->num_symbols);
}

static size_t rmt_encode_ir_sony(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                  const void *primary_data, size_t data_size,
                                  rmt_encode_state_t *ret_state) {
    rmt_ir_sony_encoder_t *sony_encoder = __containerof(encoder, rmt_ir_sony_encoder_t, base);
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    
    if (sony_encoder->state == 0) {
        ir_sony_scan_code_t *scan_code = (ir_sony_scan_code_t *)primary_data;
        build_sony_symbols(sony_encoder, scan_code);
        sony_encoder->current_symbol = 0;
        sony_encoder->state = 1;
    }
    
    while (sony_encoder->current_symbol < sony_encoder->num_symbols) {
        rmt_encode_state_t session_state = RMT_ENCODING_RESET;
        
        encoded_symbols += sony_encoder->copy_encoder->encode(
            sony_encoder->copy_encoder,
            channel,
            &sony_encoder->symbols[sony_encoder->current_symbol],
            sizeof(rmt_symbol_word_t),
            &session_state
        );
        
        if (session_state & RMT_ENCODING_COMPLETE) {
            sony_encoder->current_symbol++;
        }
        
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }
    
    sony_encoder->state = 0;
    state |= RMT_ENCODING_COMPLETE;
    
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_ir_sony_encoder(rmt_encoder_t *encoder) {
    rmt_ir_sony_encoder_t *sony_encoder = __containerof(encoder, rmt_ir_sony_encoder_t, base);
    
    if (sony_encoder->symbols) {
        free(sony_encoder->symbols);
    }
    if (sony_encoder->copy_encoder) {
        rmt_del_encoder(sony_encoder->copy_encoder);
    }
    free(sony_encoder);
    return ESP_OK;
}

static esp_err_t rmt_ir_sony_encoder_reset(rmt_encoder_t *encoder) {
    rmt_ir_sony_encoder_t *sony_encoder = __containerof(encoder, rmt_ir_sony_encoder_t, base);
    rmt_encoder_reset(sony_encoder->copy_encoder);
    sony_encoder->state = 0;
    sony_encoder->current_symbol = 0;
    
    if (sony_encoder->symbols) {
        free(sony_encoder->symbols);
        sony_encoder->symbols = NULL;
    }
    
    return ESP_OK;
}

esp_err_t rmt_new_ir_sony_encoder(const ir_sony_encoder_config_t *config,
                                   rmt_encoder_handle_t *ret_encoder) {
    esp_err_t ret = ESP_OK;
    rmt_ir_sony_encoder_t *sony_encoder = NULL;
    
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    
    sony_encoder = rmt_alloc_encoder_mem(sizeof(rmt_ir_sony_encoder_t));
    ESP_GOTO_ON_FALSE(sony_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for sony encoder");
    
    sony_encoder->base.encode = rmt_encode_ir_sony;
    sony_encoder->base.del = rmt_del_ir_sony_encoder;
    sony_encoder->base.reset = rmt_ir_sony_encoder_reset;
    sony_encoder->resolution = config->resolution;
    sony_encoder->symbols = NULL;
    sony_encoder->num_symbols = 0;
    sony_encoder->current_symbol = 0;
    sony_encoder->state = 0;
    
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &sony_encoder->copy_encoder),
                      err, TAG, "create copy encoder failed");
    
    *ret_encoder = &sony_encoder->base;
    ESP_LOGI(TAG, "Sony encoder created successfully");
    return ESP_OK;

err:
    if (sony_encoder) {
        if (sony_encoder->copy_encoder) {
            rmt_del_encoder(sony_encoder->copy_encoder);
        }
        free(sony_encoder);
    }
    return ret;
}
