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


#ifndef IR_RC5_H
#define IR_RC5_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/ledc.h"

// RC5 Protocol Constants
#define RC5_UNIT            889         // Base unit in microseconds (32 periods of 36kHz)
#define RC5_BITS            13          // Total bits (2 start + 1 toggle + 5 address + 6 command)
#define RC5_CARRIER_FREQ    36000       // 36 kHz carrier frequency
#define RC5_DUTY_CYCLE      33          // 33% duty cycle

// Configuration structure
typedef struct {
    gpio_num_t gpio_num;
    ledc_channel_t ledc_channel;
    ledc_timer_t ledc_timer;
} ir_rc5_config_t;

/**
 * @brief Initialize RC5 IR transmitter
 *
 * @param config Configuration structure with GPIO and LEDC settings
 * @return ESP_OK on success
 */
esp_err_t ir_rc5_init(const ir_rc5_config_t *config);

/**
 * @brief Send RC5 command
 *
 * @param address 5-bit address (0-31)
 * @param command 6-bit command (0-63)
 * @param toggle Toggle bit value (0 or 1)
 * @return ESP_OK on success
 */
esp_err_t ir_rc5_send(uint8_t address, uint8_t command, bool toggle);

/**
 * @brief Send extended RC5 command (7-bit command)
 *
 * @param address 5-bit address (0-31)
 * @param command 7-bit command (0-127)
 * @param toggle Toggle bit value (0 or 1)
 * @return ESP_OK on success
 */
esp_err_t ir_rc5_send_extended(uint8_t address, uint8_t command, bool toggle);

/**
 * @brief Send RC5 command with automatic toggle
 *
 * @param address 5-bit address (0-31)
 * @param command 6-bit command (0-63)
 * @return ESP_OK on success
 */
esp_err_t ir_rc5_send_auto_toggle(uint8_t address, uint8_t command);

/**
 * @brief Send extended RC5 command with automatic toggle
 *
 * @param address 5-bit address (0-31)
 * @param command 7-bit command (0-127)
 * @return ESP_OK on success
 */
esp_err_t ir_rc5_send_extended_auto_toggle(uint8_t address, uint8_t command);

/**
 * @brief Send RC5 command with automatic detection of standard/extended format
 * 
 * Automatically uses standard RC5 for commands 0-63 or extended RC5 for commands 64-127
 *
 * @param address 5-bit address (0-31)
 * @param command 7-bit command (0-127)
 * @param toggle Toggle bit value (0 or 1)
 * @return ESP_OK on success
 */
esp_err_t ir_rc5_send_auto(uint8_t address, uint8_t command, bool toggle);

/**
 * @brief Send RC5 command with automatic format detection and auto-toggle
 *
 * @param address 5-bit address (0-31)
 * @param command 7-bit command (0-127)
 * @return ESP_OK on success
 */
esp_err_t ir_rc5_send_smart(uint8_t address, uint8_t command);

/**
 * @brief Reset toggle bit to 0
 */
void ir_rc5_reset_toggle(void);

#endif // IR_RC5_H
