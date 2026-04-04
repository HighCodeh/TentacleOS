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

#ifndef ST7789_H
#define ST7789_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_H_RES          240
#define LCD_V_RES          320
#define LCD_CMD_BITS       8
#define LCD_PARAM_BITS     8

/** LCD panel handle (used by LVGL display driver). */
extern esp_lcd_panel_handle_t panel_handle;

/** LCD IO handle (used by LVGL display driver). */
extern esp_lcd_panel_io_handle_t io_handle;

/**
 * @brief Initialize the ST7789 display.
 *
 * Creates the SPI panel IO on SPI3_HOST, configures and resets the
 * ST7789 panel, inverts colors for IPS panels, initializes backlight
 * PWM, and applies saved brightness/rotation from config file.
 */
void st7789_init(void);

/**
 * @brief Fill the entire screen with a single color.
 *
 * @param color  16-bit RGB565 color value.
 */
void st7789_fill_screen(uint16_t color);

/**
 * @brief Set the backlight brightness.
 *
 * Persists the value to the display config file on the flash.
 *
 * @param percent  Brightness percentage (0-100).
 */
void lcd_set_brightness(uint8_t percent);

/**
 * @brief Get the current backlight brightness.
 *
 * @return Brightness percentage (0-100).
 */
uint8_t lcd_get_brightness(void);

/**
 * @brief Set the display rotation.
 *
 * Adjusts mirror/swap/gap settings and persists the value.
 *
 * @param rotation  Rotation index (1-4).
 */
void lcd_set_rotation(uint8_t rotation);

/**
 * @brief Get the current display rotation.
 *
 * @return Rotation index (1-4).
 */
uint8_t lcd_get_rotation(void);

#ifdef __cplusplus
}
#endif

#endif // ST7789_H
