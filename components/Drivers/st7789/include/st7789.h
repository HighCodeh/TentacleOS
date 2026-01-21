#ifndef ST7789_H
#define ST7789_H

#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_H_RES       240
#define LCD_V_RES       240
#define LCD_CMD_BITS    8
#define LCD_PARAM_BITS  8

extern esp_lcd_panel_handle_t panel_handle;
extern esp_lcd_panel_io_handle_t io_handle;

void st7789_init(void);
void st7789_fill_screen(uint16_t color);
void lcd_set_brightness(uint8_t percent);
uint8_t lcd_get_brightness(void);
void lcd_set_rotation(uint8_t rotation);
uint8_t lcd_get_rotation(void);

#endif