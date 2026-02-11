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


#ifndef IR_RC6_H
#define IR_RC6_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/ledc.h"

// RC6 Protocol Constants
#define RC6_UNIT            444         // Base unit in microseconds (16 periods of 36kHz)
#define RC6_HEADER_MARK     (6 * RC6_UNIT)  // 2664 us
#define RC6_HEADER_SPACE    (2 * RC6_UNIT)  // 888 us

#define RC6_BITS            21          // Total bits (excluding start bit)
#define RC6_TOGGLE_BIT_POS  3           // Position of toggle bit (after 3 mode bits)

#define RC6_CARRIER_FREQ    36000       // 36 kHz carrier frequency
#define RC6_DUTY_CYCLE      33          // 33% duty cycle

// Configuration structure
typedef struct {
    gpio_num_t gpio_num;
    ledc_channel_t ledc_channel;
    ledc_timer_t ledc_timer;
} ir_rc6_config_t;

/**
 * @brief Initialize RC6 IR transmitter
 * 
 * @param config Configuration structure with GPIO and LEDC settings
 * @return ESP_OK on success
 */
esp_err_t ir_rc6_init(const ir_rc6_config_t *config);

/**
 * @brief Send RC6 command
 * 
 * @param address 8-bit address
 * @param command 8-bit command
 * @param toggle Toggle bit value (0 or 1)
 * @return ESP_OK on success
 */
esp_err_t ir_rc6_send(uint8_t address, uint8_t command, bool toggle);

/**
 * @brief Send RC6 command with automatic toggle
 * 
 * @param address 8-bit address
 * @param command 8-bit command
 * @return ESP_OK on success
 */
esp_err_t ir_rc6_send_auto_toggle(uint8_t address, uint8_t command);

/**
 * @brief Reset toggle bit to 0
 */
void ir_rc6_reset_toggle(void);

#endif // IR_RC6_H
