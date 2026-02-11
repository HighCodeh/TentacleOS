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
#include "protocol_nec.h"
#include "protocol_rc6.h"
#include "protocol_rc5.h"
#include "protocol_samsung32.h"
#include "protocol_sony.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ir_encoder";

esp_err_t rmt_new_ir_encoder(const ir_encoder_config_t *cfg, rmt_encoder_handle_t *ret_encoder)
{
    if (!cfg || !ret_encoder) {
        ESP_LOGE(TAG, "Invalid arguments - cfg=%p, ret_encoder=%p", cfg, ret_encoder);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Creating encoder for protocol: %d", cfg->protocol);
    
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
    
    switch (cfg->protocol) {
        case IR_PROTOCOL_NEC:
            ESP_LOGI(TAG, "Creating NEC encoder with resolution=%lu", cfg->config.nec.resolution);
            ret = rmt_new_ir_nec_encoder(&cfg->config.nec, ret_encoder);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ NEC encoder created successfully");
            } else {
                ESP_LOGE(TAG, "❌ Failed to create NEC encoder: %s", esp_err_to_name(ret));
            }
            break;
            
        case IR_PROTOCOL_RC6:
            ESP_LOGI(TAG, "Creating RC6 encoder with resolution=%lu", cfg->config.rc6.resolution);
            ret = rmt_new_ir_rc6_encoder(&cfg->config.rc6, ret_encoder);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ RC6 encoder created successfully");
            } else {
                ESP_LOGE(TAG, "❌ Failed to create RC6 encoder: %s", esp_err_to_name(ret));
            }
            break;
            
        case IR_PROTOCOL_RC5:
            ESP_LOGI(TAG, "Creating RC5 encoder with resolution=%lu", cfg->config.rc5.resolution);
            ret = rmt_new_ir_rc5_encoder(&cfg->config.rc5, ret_encoder);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ RC5 encoder created successfully");
            } else {
                ESP_LOGE(TAG, "❌ Failed to create RC5 encoder: %s", esp_err_to_name(ret));
            }
            break;
            
        case IR_PROTOCOL_SAMSUNG32:
            ESP_LOGI(TAG, "Creating Samsung32 encoder with resolution=%lu", cfg->config.samsung32.resolution);
            ret = rmt_new_ir_samsung32_encoder(&cfg->config.samsung32, ret_encoder);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ Samsung32 encoder created successfully");
            } else {
                ESP_LOGE(TAG, "❌ Failed to create Samsung32 encoder: %s", esp_err_to_name(ret));
            }
            break;
            
        case IR_PROTOCOL_SIRC:
            ESP_LOGI(TAG, "Creating Sony SIRC encoder with resolution=%lu", cfg->config.sony.resolution);
            ret = rmt_new_ir_sony_encoder(&cfg->config.sony, ret_encoder);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ Sony SIRC encoder created successfully");
            } else {
                ESP_LOGE(TAG, "❌ Failed to create Sony encoder: %s", esp_err_to_name(ret));
            }
            break;
            
        default:
            ESP_LOGE(TAG, "Unknown protocol: %d", cfg->protocol);
            ret = ESP_ERR_INVALID_ARG;
            break;
    }
    
    return ret;
}

const char* ir_protocol_to_string(ir_protocol_t protocol) {
    switch (protocol) {
        case IR_PROTOCOL_NEC:       return "NEC";
        case IR_PROTOCOL_RC6:       return "RC6";
        case IR_PROTOCOL_RC5:       return "RC5";
        case IR_PROTOCOL_SAMSUNG32: return "Samsung32";
        case IR_PROTOCOL_SIRC:      return "SIRC";
        default:                    return "Unknown";
    }
}

ir_protocol_t ir_string_to_protocol(const char* protocol_str) {
    ESP_LOGI(TAG, "Converting string to protocol: '%s'", protocol_str);
    
    if (strcmp(protocol_str, "NEC") == 0) {
        ESP_LOGI(TAG, "Matched: NEC");
        return IR_PROTOCOL_NEC;
    }
    if (strcmp(protocol_str, "RC6") == 0) {
        ESP_LOGI(TAG, "Matched: RC6");
        return IR_PROTOCOL_RC6;
    }
    if (strcmp(protocol_str, "RC5") == 0) {
        ESP_LOGI(TAG, "Matched: RC5");
        return IR_PROTOCOL_RC5;
    }
    if (strcmp(protocol_str, "Samsung32") == 0) {
        ESP_LOGI(TAG, "Matched: Samsung32");
        return IR_PROTOCOL_SAMSUNG32;
    }
    if (strcmp(protocol_str, "SIRC") == 0 || strcmp(protocol_str, "Sony") == 0) {
        ESP_LOGI(TAG, "Matched: Sony SIRC");
        return IR_PROTOCOL_SIRC;
    }
    
    ESP_LOGW(TAG, "Unknown protocol string '%s', defaulting to NEC", protocol_str);
    return IR_PROTOCOL_NEC;
}
