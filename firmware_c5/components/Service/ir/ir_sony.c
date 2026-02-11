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


#include "ir_sony.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "IR_SONY";

// Global configuration
static ir_sony_config_t g_config;
static bool g_initialized = false;

/**
 * @brief Generate carrier wave (mark)
 */
static void ir_mark(uint32_t duration_us) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, g_config.ledc_channel, 
                  (1 << LEDC_TIMER_10_BIT) * SONY_DUTY_CYCLE / 100);
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
 * @brief Send a single bit using pulse width encoding
 * Sony uses pulse WIDTH encoding (not pulse distance):
 * - Logical '1': 1200us mark + 600us space
 * - Logical '0': 600us mark + 600us space
 */
static void send_bit(bool bit) {
    if (bit) {
        // Logical 1: long mark (2x UNIT)
        ir_mark(SONY_ONE_MARK);
    } else {
        // Logical 0: short mark (1x UNIT)
        ir_mark(SONY_ZERO_MARK);
    }
    // Space is always the same
    ir_space(SONY_SPACE);
}

esp_err_t ir_sony_init(const ir_sony_config_t *config) {
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
        .freq_hz          = SONY_CARRIER_FREQ,
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
    ESP_LOGI(TAG, "Sony SIRCS initialized on GPIO %d", g_config.gpio_num);
    
    return ESP_OK;
}

esp_err_t ir_sony_send(uint16_t address, uint8_t command, uint8_t bits) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Sony not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Validate bit count
    if (bits != SONY_BITS_12 && bits != SONY_BITS_15 && bits != SONY_BITS_20) {
        ESP_LOGE(TAG, "Invalid bit count: %d (must be 12, 15, or 20)", bits);
        return ESP_ERR_INVALID_ARG;
    }

    // Validate command (7 bits)
    if (command > 0x7F) {
        ESP_LOGE(TAG, "Command must be 0-127 (7 bits)");
        return ESP_ERR_INVALID_ARG;
    }

    // Build data frame (LSB first)
    // Format: 7 command bits + address bits (5, 8, or 13 bits)
    uint32_t data = ((uint32_t)address << 7) | (command & 0x7F);

    ESP_LOGI(TAG, "Sending Sony-%d: Addr=0x%X Cmd=0x%02X Data=0x%05lX", 
             bits, address, command, data);

    // Disable interrupts for accurate timing
    portDISABLE_INTERRUPTS();

    // Send header (start bit)
    ir_mark(SONY_HEADER_MARK);
    ir_space(SONY_SPACE);

    // Send data bits (LSB first)
    for (uint8_t i = 0; i < bits; i++) {
        bool bit = (data >> i) & 1;
        send_bit(bit);
    }

    // No stop bit in Sony protocol!

    portENABLE_INTERRUPTS();

    ESP_LOGD(TAG, "Sony transmission complete");
    
    return ESP_OK;
}

esp_err_t ir_sony_send_12(uint8_t address, uint8_t command) {
    // Validate address (5 bits for Sony-12)
    if (address > 0x1F) {
        ESP_LOGE(TAG, "Sony-12 address must be 0-31 (5 bits)");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ir_sony_send(address, command, SONY_BITS_12);
}

esp_err_t ir_sony_send_15(uint8_t address, uint8_t command) {
    // Address is 8 bits for Sony-15
    return ir_sony_send(address, command, SONY_BITS_15);
}

esp_err_t ir_sony_send_20(uint8_t device, uint8_t extended, uint8_t command) {
    // Validate device (5 bits)
    if (device > 0x1F) {
        ESP_LOGE(TAG, "Sony-20 device must be 0-31 (5 bits)");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Build 13-bit address: 8-bit extended + 5-bit device
    uint16_t address = ((uint16_t)extended << 5) | device;
    
    return ir_sony_send(address, command, SONY_BITS_20);
}

esp_err_t ir_sony_send_repeat(uint16_t address, uint8_t command, uint8_t bits, uint8_t repeats) {
    esp_err_t ret;
    
    // Send first frame
    ret = ir_sony_send(address, command, bits);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Send repeats
    for (uint8_t i = 0; i < repeats; i++) {
        // Calculate time elapsed and wait for repeat period
        // Sony repeats every 45ms from start to start
        vTaskDelay(pdMS_TO_TICKS(45));
        
        ret = ir_sony_send(address, command, bits);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    return ESP_OK;
}
