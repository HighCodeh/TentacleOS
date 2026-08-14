// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef ST7789_H
#define ST7789_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

/**
 * @brief SPI pixel clock. 20 MHz to match firmware_p4_prototype; the shared
 *        FFC / prototype hardware shows signal-integrity corruption
 *        (garbled/white images) above 20 MHz.
 */
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
#define LCD_H_RES          240
#define LCD_V_RES          320
/**
 * @brief Physical panel dimensions (used by lvgl_glue / esp_lvgl_port). Same
 *        values as LCD_H/V_RES here since the panel runs in fixed portrait.
 */
#define LCD_PANEL_W    240
#define LCD_PANEL_H    320
#define LCD_CMD_BITS   8
#define LCD_PARAM_BITS 8

/** @brief LCD panel handle (used by LVGL display driver). */
extern esp_lcd_panel_handle_t panel_handle;

/** @brief LCD IO handle (used by LVGL display driver). */
extern esp_lcd_panel_io_handle_t io_handle;

/**
 * @brief Initialize the ST7789 display.
 *
 * Creates the SPI panel IO on SPI3_HOST, configures and resets the
 * ST7789 panel, inverts colors for IPS panels, initializes backlight
 * PWM, and applies saved brightness/rotation from config file.
 *
 * @return ESP_OK on success, or an esp_err_t from the panel IO/init on failure.
 */
esp_err_t st7789_init(void);

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
 * @brief Apply a backlight level WITHOUT persisting it.
 *
 * Used for transient changes (auto-dim / fade) so the user's saved brightness is
 * preserved and can be restored exactly.
 *
 * @param percent  Brightness percentage (0-100).
 */
void lcd_apply_brightness(uint8_t percent);

/**
 * @brief Get the current backlight brightness.
 *
 * @return Brightness percentage (0-100).
 */
uint8_t lcd_get_brightness(void);

/**
 * @brief Put the display panel to sleep (pixels off) or wake it.
 *
 * Cuts the panel itself, not just the backlight. Pair with backlight off/restore
 * for a real screen-off.
 *
 * @param sleep  true = panel off, false = panel on.
 */
void lcd_display_sleep(bool sleep);

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
