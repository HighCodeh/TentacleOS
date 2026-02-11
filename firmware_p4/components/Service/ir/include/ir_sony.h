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


#ifndef IR_SONY_H
#define IR_SONY_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/ledc.h"

// Sony Protocol Constants
#define SONY_UNIT           600         // Base unit in microseconds (24 periods of 40kHz)
#define SONY_HEADER_MARK    (4 * SONY_UNIT)  // 2400 us
#define SONY_ONE_MARK       (2 * SONY_UNIT)  // 1200 us
#define SONY_ZERO_MARK      SONY_UNIT        // 600 us
#define SONY_SPACE          SONY_UNIT        // 600 us

#define SONY_CARRIER_FREQ   40000       // 40 kHz carrier frequency
#define SONY_DUTY_CYCLE     33          // 33% duty cycle

// Protocol bit lengths
#define SONY_BITS_12        12          // 7 command + 5 address
#define SONY_BITS_15        15          // 7 command + 8 address
#define SONY_BITS_20        20          // 7 command + 13 address (5 device + 8 extended)

#define SONY_REPEAT_PERIOD  45000       // Commands repeated every 45ms

// Protocol types
typedef enum {
    SONY_12 = 12,                       // Standard Sony 12-bit
    SONY_15 = 15,                       // Sony 15-bit
    SONY_20 = 20                        // Sony 20-bit (with extended)
} sony_protocol_t;

// Configuration structure
typedef struct {
    gpio_num_t gpio_num;
    ledc_channel_t ledc_channel;
    ledc_timer_t ledc_timer;
} ir_sony_config_t;

/**
 * @brief Initialize Sony IR transmitter
 *
 * @param config Configuration structure with GPIO and LEDC settings
 * @return ESP_OK on success
 */
esp_err_t ir_sony_init(const ir_sony_config_t *config);

/**
 * @brief Send Sony SIRCS command with specific bit count
 *
 * @param address Address/Device code
 * @param command 7-bit command (0-127)
 * @param bits Number of bits (12, 15, or 20)
 * @return ESP_OK on success
 */
esp_err_t ir_sony_send(uint16_t address, uint8_t command, uint8_t bits);

/**
 * @brief Send Sony SIRCS-12 command (7 command + 5 address bits)
 *
 * @param address 5-bit address (0-31)
 * @param command 7-bit command (0-127)
 * @return ESP_OK on success
 */
esp_err_t ir_sony_send_12(uint8_t address, uint8_t command);

/**
 * @brief Send Sony SIRCS-15 command (7 command + 8 address bits)
 *
 * @param address 8-bit address (0-255)
 * @param command 7-bit command (0-127)
 * @return ESP_OK on success
 */
esp_err_t ir_sony_send_15(uint8_t address, uint8_t command);

/**
 * @brief Send Sony SIRCS-20 command (7 command + 5 device + 8 extended bits)
 *
 * @param device 5-bit device code (0-31)
 * @param extended 8-bit extended code (0-255)
 * @param command 7-bit command (0-127)
 * @return ESP_OK on success
 */
esp_err_t ir_sony_send_20(uint8_t device, uint8_t extended, uint8_t command);

/**
 * @brief Send Sony command with automatic repeats
 * 
 * Sony protocol typically sends commands 3 times (2 repeats)
 *
 * @param address Address/Device code
 * @param command 7-bit command (0-127)
 * @param bits Number of bits (12, 15, or 20)
 * @param repeats Number of repeats (default: 2 for 3 total sends)
 * @return ESP_OK on success
 */
esp_err_t ir_sony_send_repeat(uint16_t address, uint8_t command, uint8_t bits, uint8_t repeats);

#endif // IR_SONY_H
