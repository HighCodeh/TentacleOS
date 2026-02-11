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
#include "protocol_rc6.h"
#include "esp_check.h"
#include <string.h>

static const char *TAG = "rc6_encoder";

// Timings RC6 em microsegundos
#define RC6_UNIT                 444    // Unidade base
#define RC6_HEADER_MARK         2666   // 6T
#define RC6_HEADER_SPACE         889   // 2T

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *copy_encoder;
    rmt_symbol_word_t *symbols;
    size_t num_symbols;
    size_t current_symbol;
    uint32_t resolution;
    int state;
} rmt_ir_rc6_encoder_t;

static inline uint32_t us_to_ticks(uint32_t us, uint32_t resolution) {
    return (us * resolution) / 1000000;
}

static void build_rc6_symbols(rmt_ir_rc6_encoder_t *encoder, const ir_rc6_scan_code_t *scan_code) {
    uint32_t res = encoder->resolution;
    size_t idx = 0;
    
    // Construir frame: Mode(3) + Toggle(1) + Address(8) + Command(8) = 20 bits
    uint32_t frame = 0;
    frame |= (0x0 << 17);  // Mode 000
    frame |= ((scan_code->toggle & 0x1) << 16);
    frame |= ((uint32_t)scan_code->address << 8);
    frame |= scan_code->command;
    
    ESP_LOGI(TAG, "Building RC6 frame: 0x%05lX (addr=0x%02X, cmd=0x%02X, toggle=%d)",
             frame, scan_code->address, scan_code->command, scan_code->toggle);
    
    // Aloca array de símbolos (header + start + 20 bits data * 2 + margem)
    encoder->symbols = malloc(sizeof(rmt_symbol_word_t) * 50);
    if (!encoder->symbols) {
        ESP_LOGE(TAG, "Failed to allocate symbols");
        return;
    }
    
    // Header: 6T mark + 2T space
    encoder->symbols[idx++] = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = us_to_ticks(RC6_HEADER_MARK, res),
        .level1 = 0,
        .duration1 = us_to_ticks(RC6_HEADER_SPACE, res)
    };
    
    // Start bit: sempre 1 (1T mark + 1T space)
    encoder->symbols[idx++] = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = us_to_ticks(RC6_UNIT, res),
        .level1 = 0,
        .duration1 = us_to_ticks(RC6_UNIT, res)
    };
    
    // Enviar 20 bits de dados (MSB first) com Manchester encoding
    for (int i = 19; i >= 0; i--) {
        bool bit = (frame >> i) & 1;
        bool is_toggle = (i == 16);  // Bit 16 é o toggle (double width)
        
        uint32_t duration = is_toggle ? (RC6_UNIT * 2) : RC6_UNIT;
        
        if (bit) {
            // Bit 1: mark + space (Manchester)
            encoder->symbols[idx++] = (rmt_symbol_word_t) {
                .level0 = 1,
                .duration0 = us_to_ticks(duration, res),
                .level1 = 0,
                .duration1 = us_to_ticks(duration, res)
            };
        } else {
            // Bit 0: space + mark
            encoder->symbols[idx++] = (rmt_symbol_word_t) {
                .level0 = 0,
                .duration0 = us_to_ticks(duration, res),
                .level1 = 1,
                .duration1 = us_to_ticks(duration, res)
            };
        }
    }
    
    // Ending space
    encoder->symbols[idx++] = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = us_to_ticks(RC6_UNIT, res),
        .level1 = 0,
        .duration1 = 0
    };
    
    encoder->num_symbols = idx;
    ESP_LOGI(TAG, "Built %zu symbols", encoder->num_symbols);
}

static size_t rmt_encode_ir_rc6(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                 const void *primary_data, size_t data_size,
                                 rmt_encode_state_t *ret_state) {
    rmt_ir_rc6_encoder_t *rc6_encoder = __containerof(encoder, rmt_ir_rc6_encoder_t, base);
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    
    if (rc6_encoder->state == 0) {
        // Primeira vez - construir símbolos
        ir_rc6_scan_code_t *scan_code = (ir_rc6_scan_code_t *)primary_data;
        build_rc6_symbols(rc6_encoder, scan_code);
        rc6_encoder->current_symbol = 0;
        rc6_encoder->state = 1;
    }
    
    // Enviar símbolos um por um
    while (rc6_encoder->current_symbol < rc6_encoder->num_symbols) {
        rmt_encode_state_t session_state = RMT_ENCODING_RESET;
        
        encoded_symbols += rc6_encoder->copy_encoder->encode(
            rc6_encoder->copy_encoder,
            channel,
            &rc6_encoder->symbols[rc6_encoder->current_symbol],
            sizeof(rmt_symbol_word_t),
            &session_state
        );
        
        if (session_state & RMT_ENCODING_COMPLETE) {
            rc6_encoder->current_symbol++;
        }
        
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }
    
    // Tudo enviado
    rc6_encoder->state = 0;
    state |= RMT_ENCODING_COMPLETE;
    
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_ir_rc6_encoder(rmt_encoder_t *encoder) {
    rmt_ir_rc6_encoder_t *rc6_encoder = __containerof(encoder, rmt_ir_rc6_encoder_t, base);
    
    if (rc6_encoder->symbols) {
        free(rc6_encoder->symbols);
    }
    if (rc6_encoder->copy_encoder) {
        rmt_del_encoder(rc6_encoder->copy_encoder);
    }
    free(rc6_encoder);
    return ESP_OK;
}

static esp_err_t rmt_ir_rc6_encoder_reset(rmt_encoder_t *encoder) {
    rmt_ir_rc6_encoder_t *rc6_encoder = __containerof(encoder, rmt_ir_rc6_encoder_t, base);
    rmt_encoder_reset(rc6_encoder->copy_encoder);
    rc6_encoder->state = 0;
    rc6_encoder->current_symbol = 0;
    
    if (rc6_encoder->symbols) {
        free(rc6_encoder->symbols);
        rc6_encoder->symbols = NULL;
    }
    
    return ESP_OK;
}

esp_err_t rmt_new_ir_rc6_encoder(const ir_rc6_encoder_config_t *config, 
                                   rmt_encoder_handle_t *ret_encoder) {
    esp_err_t ret = ESP_OK;
    rmt_ir_rc6_encoder_t *rc6_encoder = NULL;
    
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    
    rc6_encoder = rmt_alloc_encoder_mem(sizeof(rmt_ir_rc6_encoder_t));
    ESP_GOTO_ON_FALSE(rc6_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for rc6 encoder");
    
    rc6_encoder->base.encode = rmt_encode_ir_rc6;
    rc6_encoder->base.del = rmt_del_ir_rc6_encoder;
    rc6_encoder->base.reset = rmt_ir_rc6_encoder_reset;
    rc6_encoder->resolution = config->resolution;
    rc6_encoder->symbols = NULL;
    rc6_encoder->num_symbols = 0;
    rc6_encoder->current_symbol = 0;
    rc6_encoder->state = 0;
    
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &rc6_encoder->copy_encoder),
                      err, TAG, "create copy encoder failed");
    
    *ret_encoder = &rc6_encoder->base;
    ESP_LOGI(TAG, "RC6 encoder created successfully");
    return ESP_OK;

err:
    if (rc6_encoder) {
        if (rc6_encoder->copy_encoder) {
            rmt_del_encoder(rc6_encoder->copy_encoder);
        }
        free(rc6_encoder);
    }
    return ret;
}
