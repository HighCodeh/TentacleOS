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


#include "ir_rc5.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "IR_RC5";

// Global configuration
static ir_rc5_config_t g_config;
static bool g_initialized = false;
static uint8_t g_toggle_state = 0;

/**
 * @brief Generate carrier wave (mark)
 */
static void ir_mark(uint32_t duration_us) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, g_config.ledc_channel, 
                  (1 << LEDC_TIMER_10_BIT) * RC5_DUTY_CYCLE / 100);
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
 * @brief Send Manchester encoded bit (RC5 style)
 * RC5 Manchester encoding:
 * - Logical '1': Space->Mark (889us each)
 * - Logical '0': Mark->Space (889us each)
 */
static void send_bit(bool bit) {
    if (bit) {
        // Logical 1: Space then Mark
        ir_space(RC5_UNIT);
        ir_mark(RC5_UNIT);
    } else {
        // Logical 0: Mark then Space
        ir_mark(RC5_UNIT);
        ir_space(RC5_UNIT);
    }
}

esp_err_t ir_rc5_init(const ir_rc5_config_t *config) {
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
        .freq_hz          = RC5_CARRIER_FREQ,
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
    ESP_LOGI(TAG, "RC5 initialized on GPIO %d", g_config.gpio_num);
    
    return ESP_OK;
}

esp_err_t ir_rc5_send(uint8_t address, uint8_t command, bool toggle) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "RC5 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Validate parameters
    if (address > 31) {
        ESP_LOGE(TAG, "Address must be 0-31 (5 bits)");
        return ESP_ERR_INVALID_ARG;
    }
    if (command > 63) {
        ESP_LOGE(TAG, "Command must be 0-63 (6 bits)");
        return ESP_ERR_INVALID_ARG;
    }

    // Build RC5 data frame (13 bits total)
    // Format: 2 start bits (both 1) + 1 toggle bit + 5 address bits + 6 command bits
    uint16_t data = 0;
    
    // Start bits (always 11)
    data |= (0x3 << 11);  // 2 bits at position 11-12
    
    // Toggle bit
    data |= ((toggle ? 1 : 0) << 10);
    
    // Address (5 bits)
    data |= ((uint16_t)(address & 0x1F) << 6);
    
    // Command (6 bits)
    data |= (command & 0x3F);

    ESP_LOGI(TAG, "Sending RC5: Addr=0x%02X Cmd=0x%02X Toggle=%d Data=0x%04X", 
             address, command, toggle, data);

    // Disable interrupts for accurate timing
    portDISABLE_INTERRUPTS();

    // Send all bits (MSB first)
    for (int8_t i = 12; i >= 0; i--) {
        bool bit = (data >> i) & 1;
        send_bit(bit);
    }

    // Ensure output is low after transmission
    ir_space(RC5_UNIT);

    portENABLE_INTERRUPTS();

    ESP_LOGD(TAG, "RC5 transmission complete");
    
    return ESP_OK;
}

esp_err_t ir_rc5_send_extended(uint8_t address, uint8_t command, bool toggle) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "RC5 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Validate parameters
    if (address > 31) {
        ESP_LOGE(TAG, "Address must be 0-31 (5 bits)");
        return ESP_ERR_INVALID_ARG;
    }
    if (command > 127) {
        ESP_LOGE(TAG, "Extended command must be 0-127 (7 bits)");
        return ESP_ERR_INVALID_ARG;
    }

    // Build Extended RC5 data frame (13 bits)
    // The IRremote library format for extended commands:
    // Bit 12: Start bit (always 1)
    // Bit 11: Field bit (inverted bit 6 of command) - this extends to 7-bit commands
    // Bit 10: Toggle bit
    // Bits 9-6: Address (5 bits, but we only use 4 bits here, bit 9 is part of extended format)
    // Bits 5-0: Command (lower 6 bits)
    
    uint16_t data = 0;
    
    // Start bit (always 1)
    data |= (0x1 << 12);
    
    // Field bit (inverted bit 6 of command)
    // If command >= 64 (bit 6 = 1), field bit = 0
    // If command < 64 (bit 6 = 0), field bit = 1
    bool field_bit = !(command & 0x40);
    data |= ((field_bit ? 1 : 0) << 11);
    
    // Toggle bit
    data |= ((toggle ? 1 : 0) << 10);
    
    // Address (5 bits)
    data |= ((uint16_t)(address & 0x1F) << 6);
    
    // Command lower 6 bits
    data |= (command & 0x3F);

    ESP_LOGI(TAG, "Sending Extended RC5: Addr=0x%02X Cmd=0x%02X Toggle=%d FieldBit=%d Data=0x%03X", 
             address, command, toggle, field_bit, data);

    // Disable interrupts for accurate timing
    portDISABLE_INTERRUPTS();

    // Send all bits (MSB first)
    for (int8_t i = 12; i >= 0; i--) {
        bool bit = (data >> i) & 1;
        send_bit(bit);
    }

    // Ensure output is low after transmission
    ir_space(RC5_UNIT);

    portENABLE_INTERRUPTS();

    ESP_LOGD(TAG, "Extended RC5 transmission complete");
    
    return ESP_OK;
}

esp_err_t ir_rc5_send_auto_toggle(uint8_t address, uint8_t command) {
    esp_err_t ret = ir_rc5_send(address, command, g_toggle_state);
    
    // Toggle for next transmission
    g_toggle_state = !g_toggle_state;
    
    return ret;
}

esp_err_t ir_rc5_send_extended_auto_toggle(uint8_t address, uint8_t command) {
    esp_err_t ret = ir_rc5_send_extended(address, command, g_toggle_state);
    
    // Toggle for next transmission
    g_toggle_state = !g_toggle_state;
    
    return ret;
}

esp_err_t ir_rc5_send_auto(uint8_t address, uint8_t command, bool toggle) {
    // Automatically choose standard or extended format based on command value
    if (command <= 63) {
        return ir_rc5_send(address, command, toggle);
    } else {
        return ir_rc5_send_extended(address, command, toggle);
    }
}

esp_err_t ir_rc5_send_smart(uint8_t address, uint8_t command) {
    esp_err_t ret = ir_rc5_send_auto(address, command, g_toggle_state);
    
    // Toggle for next transmission
    g_toggle_state = !g_toggle_state;
    
    return ret;
}

void ir_rc5_reset_toggle(void) {
    g_toggle_state = 0;
    ESP_LOGD(TAG, "Toggle bit reset");
}
