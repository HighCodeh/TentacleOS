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
#include "protocol_rc5.h"
#include "esp_check.h"
#include <string.h>

static const char *TAG = "rc5_encoder";

// Timings RC5 em microsegundos
#define RC5_UNIT                 889    // Unidade base (Manchester)

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *copy_encoder;
    rmt_symbol_word_t *symbols;
    size_t num_symbols;
    size_t current_symbol;
    uint32_t resolution;
    int state;
} rmt_ir_rc5_encoder_t;

static inline uint32_t us_to_ticks(uint32_t us, uint32_t resolution) {
    return (us * resolution) / 1000000;
}

static void build_rc5_symbols(rmt_ir_rc5_encoder_t *encoder, const ir_rc5_scan_code_t *scan_code) {
    uint32_t res = encoder->resolution;
    size_t idx = 0;
    
    // Construir frame: 2 start bits + 1 toggle + 5 address + 6 command = 14 bits
    uint16_t frame = 0;
    frame |= (0x3 << 11);  // 2 start bits (sempre 11)
    frame |= ((scan_code->toggle & 0x1) << 10);
    frame |= ((scan_code->address & 0x1F) << 6);
    frame |= (scan_code->command & 0x3F);
    
    ESP_LOGI(TAG, "Building RC5 frame: 0x%04X (addr=0x%02X, cmd=0x%02X, toggle=%d)",
             frame, scan_code->address, scan_code->command, scan_code->toggle);
    
    // Aloca símbolos (14 bits * 2 transições cada = 28 símbolos máximo)
    encoder->symbols = malloc(sizeof(rmt_symbol_word_t) * 30);
    if (!encoder->symbols) {
        ESP_LOGE(TAG, "Failed to allocate symbols");
        return;
    }
    
    // Manchester encoding: bit 1 = space->mark, bit 0 = mark->space
    for (int i = 12; i >= 0; i--) {  // 13 bits (2 start + 1 toggle + 5 addr + 6 cmd)
        bool bit = (frame >> i) & 1;
        
        if (bit) {
            // Bit 1: space (low) -> mark (high)
            encoder->symbols[idx++] = (rmt_symbol_word_t) {
                .level0 = 0,
                .duration0 = us_to_ticks(RC5_UNIT, res),
                .level1 = 1,
                .duration1 = us_to_ticks(RC5_UNIT, res)
            };
        } else {
            // Bit 0: mark (high) -> space (low)
            encoder->symbols[idx++] = (rmt_symbol_word_t) {
                .level0 = 1,
                .duration0 = us_to_ticks(RC5_UNIT, res),
                .level1 = 0,
                .duration1 = us_to_ticks(RC5_UNIT, res)
            };
        }
    }
    
    // Ending space
    encoder->symbols[idx++] = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = us_to_ticks(RC5_UNIT, res),
        .level1 = 0,
        .duration1 = 0
    };
    
    encoder->num_symbols = idx;
    ESP_LOGI(TAG, "Built %zu symbols", encoder->num_symbols);
}

static size_t rmt_encode_ir_rc5(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                 const void *primary_data, size_t data_size,
                                 rmt_encode_state_t *ret_state) {
    rmt_ir_rc5_encoder_t *rc5_encoder = __containerof(encoder, rmt_ir_rc5_encoder_t, base);
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    
    if (rc5_encoder->state == 0) {
        ir_rc5_scan_code_t *scan_code = (ir_rc5_scan_code_t *)primary_data;
        build_rc5_symbols(rc5_encoder, scan_code);
        rc5_encoder->current_symbol = 0;
        rc5_encoder->state = 1;
    }
    
    while (rc5_encoder->current_symbol < rc5_encoder->num_symbols) {
        rmt_encode_state_t session_state = RMT_ENCODING_RESET;
        
        encoded_symbols += rc5_encoder->copy_encoder->encode(
            rc5_encoder->copy_encoder,
            channel,
            &rc5_encoder->symbols[rc5_encoder->current_symbol],
            sizeof(rmt_symbol_word_t),
            &session_state
        );
        
        if (session_state & RMT_ENCODING_COMPLETE) {
            rc5_encoder->current_symbol++;
        }
        
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }
    
    rc5_encoder->state = 0;
    state |= RMT_ENCODING_COMPLETE;
    
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_ir_rc5_encoder(rmt_encoder_t *encoder) {
    rmt_ir_rc5_encoder_t *rc5_encoder = __containerof(encoder, rmt_ir_rc5_encoder_t, base);
    
    if (rc5_encoder->symbols) {
        free(rc5_encoder->symbols);
    }
    if (rc5_encoder->copy_encoder) {
        rmt_del_encoder(rc5_encoder->copy_encoder);
    }
    free(rc5_encoder);
    return ESP_OK;
}

static esp_err_t rmt_ir_rc5_encoder_reset(rmt_encoder_t *encoder) {
    rmt_ir_rc5_encoder_t *rc5_encoder = __containerof(encoder, rmt_ir_rc5_encoder_t, base);
    rmt_encoder_reset(rc5_encoder->copy_encoder);
    rc5_encoder->state = 0;
    rc5_encoder->current_symbol = 0;
    
    if (rc5_encoder->symbols) {
        free(rc5_encoder->symbols);
        rc5_encoder->symbols = NULL;
    }
    
    return ESP_OK;
}

esp_err_t rmt_new_ir_rc5_encoder(const ir_rc5_encoder_config_t *config, 
                                   rmt_encoder_handle_t *ret_encoder) {
    esp_err_t ret = ESP_OK;
    rmt_ir_rc5_encoder_t *rc5_encoder = NULL;
    
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    
    rc5_encoder = rmt_alloc_encoder_mem(sizeof(rmt_ir_rc5_encoder_t));
    ESP_GOTO_ON_FALSE(rc5_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for rc5 encoder");
    
    rc5_encoder->base.encode = rmt_encode_ir_rc5;
    rc5_encoder->base.del = rmt_del_ir_rc5_encoder;
    rc5_encoder->base.reset = rmt_ir_rc5_encoder_reset;
    rc5_encoder->resolution = config->resolution;
    rc5_encoder->symbols = NULL;
    rc5_encoder->num_symbols = 0;
    rc5_encoder->current_symbol = 0;
    rc5_encoder->state = 0;
    
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &rc5_encoder->copy_encoder),
                      err, TAG, "create copy encoder failed");
    
    *ret_encoder = &rc5_encoder->base;
    ESP_LOGI(TAG, "RC5 encoder created successfully");
    return ESP_OK;

err:
    if (rc5_encoder) {
        if (rc5_encoder->copy_encoder) {
            rmt_del_encoder(rc5_encoder->copy_encoder);
        }
        free(rc5_encoder);
    }
    return ret;
}
