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

void lcd_set_brightness(uint8_t percent);
void st7789_init(void);

#endif // ST7789_H
