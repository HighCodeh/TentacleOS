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

#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Initialize the addressable RGB LED (WS2812 / SK6812).
 *
 * Configures the RMT peripheral to drive the LED on the GPIO
 * defined in pin_def.h (GPIO_LED_RGB_PIN). Clears the LED on startup.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_FAIL if RMT driver setup fails
 */
esp_err_t led_rgb_init(void);

/**
 * @brief Set the LED to a specific color.
 *
 * @param r  Red intensity (0-255).
 * @param g  Green intensity (0-255).
 * @param b  Blue intensity (0-255).
 */
void led_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn off the LED.
 */
void led_clear(void);

/**
 * @brief Blink the LED once with a given color and duration.
 *
 * Sets the color, waits for duration_ms, then turns the LED off.
 * This function blocks for the duration of the blink.
 *
 * @param r            Red intensity (0-255).
 * @param g            Green intensity (0-255).
 * @param b            Blue intensity (0-255).
 * @param duration_ms  Blink duration in milliseconds.
 */
void led_blink(uint8_t r, uint8_t g, uint8_t b, int duration_ms);

/**
 * @brief Blink red (error indicator, 500 ms).
 */
void led_blink_red(void);

/**
 * @brief Blink green (success indicator, 220 ms).
 */
void led_blink_green(void);

/**
 * @brief Blink blue (info indicator, 500 ms).
 */
void led_blink_blue(void);

/**
 * @brief Blink purple (info indicator, 500 ms).
 */
void led_blink_purple(void);

#ifdef __cplusplus
}
#endif

#endif // LED_CONTROL_H
