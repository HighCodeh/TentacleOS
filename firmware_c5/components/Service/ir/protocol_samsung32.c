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
#include "protocol_samsung32.h"
#include "esp_check.h"
#include <string.h>

static const char *TAG = "samsung32_encoder";

// Timings Samsung32 em microsegundos
#define SAMSUNG32_LEADING_MARK    4500
#define SAMSUNG32_LEADING_SPACE   4500
#define SAMSUNG32_BIT_MARK         560
#define SAMSUNG32_BIT_ZERO_SPACE   560
#define SAMSUNG32_BIT_ONE_SPACE   1690
#define SAMSUNG32_ENDING_MARK      560

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *copy_encoder;
    rmt_encoder_t *bytes_encoder;
    rmt_symbol_word_t samsung32_leading_symbol;
    rmt_symbol_word_t samsung32_ending_symbol;
    int state;
} rmt_ir_samsung32_encoder_t;

static size_t rmt_encode_ir_samsung32(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                       const void *primary_data, size_t data_size,
                                       rmt_encode_state_t *ret_state) {
    rmt_ir_samsung32_encoder_t *samsung32_encoder = __containerof(encoder, rmt_ir_samsung32_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    ir_samsung32_scan_code_t *scan_code = (ir_samsung32_scan_code_t *)primary_data;
    rmt_encoder_handle_t copy_encoder = samsung32_encoder->copy_encoder;
    rmt_encoder_handle_t bytes_encoder = samsung32_encoder->bytes_encoder;
    
    switch (samsung32_encoder->state) {
    case 0: // send leading code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, 
                                                &samsung32_encoder->samsung32_leading_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            samsung32_encoder->state = 1;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    // fall-through
    case 1: // send 32-bit data (LSB first)
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, 
                                                 &scan_code->data, sizeof(uint32_t), 
                                                 &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            samsung32_encoder->state = 2;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    // fall-through
    case 2: // send ending code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, 
                                                &samsung32_encoder->samsung32_ending_symbol,
                                                sizeof(rmt_symbol_word_t), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            samsung32_encoder->state = RMT_ENCODING_RESET;
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

static esp_err_t rmt_del_ir_samsung32_encoder(rmt_encoder_t *encoder) {
    rmt_ir_samsung32_encoder_t *samsung32_encoder = __containerof(encoder, rmt_ir_samsung32_encoder_t, base);
    rmt_del_encoder(samsung32_encoder->copy_encoder);
    rmt_del_encoder(samsung32_encoder->bytes_encoder);
    free(samsung32_encoder);
    return ESP_OK;
}

static esp_err_t rmt_ir_samsung32_encoder_reset(rmt_encoder_t *encoder) {
    rmt_ir_samsung32_encoder_t *samsung32_encoder = __containerof(encoder, rmt_ir_samsung32_encoder_t, base);
    rmt_encoder_reset(samsung32_encoder->copy_encoder);
    rmt_encoder_reset(samsung32_encoder->bytes_encoder);
    samsung32_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

esp_err_t rmt_new_ir_samsung32_encoder(const ir_samsung32_encoder_config_t *config,
                                        rmt_encoder_handle_t *ret_encoder) {
    esp_err_t ret = ESP_OK;
    rmt_ir_samsung32_encoder_t *samsung32_encoder = NULL;
    
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    
    samsung32_encoder = rmt_alloc_encoder_mem(sizeof(rmt_ir_samsung32_encoder_t));
    ESP_GOTO_ON_FALSE(samsung32_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for samsung32 encoder");
    
    samsung32_encoder->base.encode = rmt_encode_ir_samsung32;
    samsung32_encoder->base.del = rmt_del_ir_samsung32_encoder;
    samsung32_encoder->base.reset = rmt_ir_samsung32_encoder_reset;

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &samsung32_encoder->copy_encoder),
                      err, TAG, "create copy encoder failed");

    // Leading code (AGC burst)
    samsung32_encoder->samsung32_leading_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = SAMSUNG32_LEADING_MARK * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = SAMSUNG32_LEADING_SPACE * config->resolution / 1000000,
    };
    
    // Ending mark
    samsung32_encoder->samsung32_ending_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = SAMSUNG32_ENDING_MARK * config->resolution / 1000000,
        .level1 = 0,
        .duration1 = 0x7FFF,
    };

    // Bytes encoder para os 32 bits de dados
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = SAMSUNG32_BIT_MARK * config->resolution / 1000000,
            .level1 = 0,
            .duration1 = SAMSUNG32_BIT_ZERO_SPACE * config->resolution / 1000000,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = SAMSUNG32_BIT_MARK * config->resolution / 1000000,
            .level1 = 0,
            .duration1 = SAMSUNG32_BIT_ONE_SPACE * config->resolution / 1000000,
        },
        .flags.msb_first = 0  // LSB first
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &samsung32_encoder->bytes_encoder),
                      err, TAG, "create bytes encoder failed");

    *ret_encoder = &samsung32_encoder->base;
    ESP_LOGI(TAG, "Samsung32 encoder created successfully");
    return ESP_OK;

err:
    if (samsung32_encoder) {
        if (samsung32_encoder->bytes_encoder) {
            rmt_del_encoder(samsung32_encoder->bytes_encoder);
        }
        if (samsung32_encoder->copy_encoder) {
            rmt_del_encoder(samsung32_encoder->copy_encoder);
        }
        free(samsung32_encoder);
    }
    return ret;
}
