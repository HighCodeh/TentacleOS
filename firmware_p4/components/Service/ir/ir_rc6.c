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


#include "ir_rc6.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "IR_RC6";

// Global configuration
static ir_rc6_config_t g_config;
static bool g_initialized = false;
static uint8_t g_toggle_state = 0;

/**
 * @brief Generate carrier wave (mark)
 */
static void ir_mark(uint32_t duration_us) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, g_config.ledc_channel, 
                  (1 << LEDC_TIMER_10_BIT) * RC6_DUTY_CYCLE / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, g_config.ledc_channel);
    esp_rom_delay_us(duration_us);
}

/**
 * @brief Generate space (no carrier)
 */
static void ir_space(uint32_t duration_us) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, g_config.ledc_channel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, g_config.ledc_channel);
    esp_rom_delay_us(duration_us);
}

/**
 * @brief Send Manchester encoded bit
 * Mark->Space = 1
 * Space->Mark = 0
 */
static void send_bit(bool bit, uint32_t duration) {
    if (bit) {
        ir_mark(duration);
        ir_space(duration);
    } else {
        ir_space(duration);
        ir_mark(duration);
    }
}

esp_err_t ir_rc6_init(const ir_rc6_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    g_config = *config;

    // Configure LEDC timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = g_config.ledc_timer,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = RC6_CARRIER_FREQ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = g_config.ledc_channel,
        .timer_sel      = g_config.ledc_timer,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = g_config.gpio_num,
        .duty           = 0,
        .hpoint         = 0
    };
    
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "RC6 initialized on GPIO %d", g_config.gpio_num);
    
    return ESP_OK;
}

esp_err_t ir_rc6_send(uint8_t address, uint8_t command, bool toggle) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "RC6 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Build RC6 data frame
    // Format: 3 mode bits (000) + 1 toggle bit + 8 address bits + 8 command bits
    uint32_t data = 0;
    
    // Mode bits (always 000 for standard RC6)
    data |= (0x0 << 17);  // 3 bits at position 17-19
    
    // Toggle bit
    data |= ((toggle ? 1 : 0) << 16);
    
    // Address (8 bits)
    data |= ((uint32_t)address << 8);
    
    // Command (8 bits)
    data |= command;

    ESP_LOGI(TAG, "Sending RC6: Addr=0x%02X Cmd=0x%02X Toggle=%d Data=0x%05lX", 
             address, command, toggle, data);

    // Disable interrupts for accurate timing
    portDISABLE_INTERRUPTS();

    // Send header
    ir_mark(RC6_HEADER_MARK);
    ir_space(RC6_HEADER_SPACE);

    // Send start bit (always 1)
    ir_mark(RC6_UNIT);
    ir_space(RC6_UNIT);

    // Send data bits (MSB first)
    for (int8_t i = 19; i >= 0; i--) {
        bool bit = (data >> i) & 1;
        
        // Toggle bit is double width
        if (i == 16) {
            send_bit(bit, RC6_UNIT * 2);
        } else {
            send_bit(bit, RC6_UNIT);
        }
    }

    // Final space to complete last bit
    ir_space(RC6_UNIT);

    portENABLE_INTERRUPTS();

    ESP_LOGD(TAG, "RC6 transmission complete");
    
    return ESP_OK;
}

esp_err_t ir_rc6_send_auto_toggle(uint8_t address, uint8_t command) {
    esp_err_t ret = ir_rc6_send(address, command, g_toggle_state);
    
    // Toggle for next transmission
    g_toggle_state = !g_toggle_state;
    
    return ret;
}

void ir_rc6_reset_toggle(void) {
    g_toggle_state = 0;
    ESP_LOGD(TAG, "Toggle bit reset");
}
