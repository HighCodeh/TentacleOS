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

#include <stdio.h>
#include <stdint.h>
#include "st7789.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/spi_types.h"
#include "pin_def.h"
#include "spi.h"
#include "driver/gpio.h"
#include "driver/ledc.h" // Adicionado para controle de PWM
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

// PWM (Backlight)
#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BL_LEDC_CH      LEDC_CHANNEL_0
#define BL_LEDC_RES     LEDC_TIMER_13_BIT // Resolução de 13 bits (0-8191)
#define BL_LEDC_FREQ    5000              // 5 kHz (sem cintilação visível)


static const char *TAG = "ST7789_DRIVER";

esp_lcd_panel_io_handle_t io_handle = NULL; 
esp_lcd_panel_handle_t panel_handle = NULL;

static void init_backlight_pwm(void)
{
    ESP_LOGI(TAG, "Configurando PWM do Backlight...");
    
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = BL_LEDC_MODE,
        .timer_num        = BL_LEDC_TIMER,
        .duty_resolution  = BL_LEDC_RES,
        .freq_hz          = BL_LEDC_FREQ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = BL_LEDC_MODE,
        .channel        = BL_LEDC_CH,
        .timer_sel      = BL_LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = ST7789_PIN_BL,
        .duty           = 0, // Começa desligado
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void lcd_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    
    uint32_t duty = (8191 * percent) / 100;

    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CH, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CH);
}

void st7789_init(void)
{
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = ST7789_PIN_DC,
        .cs_gpio_num = ST7789_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = ST7789_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_LOGI(TAG, "Inicializando Display...");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    init_backlight_pwm();
    
    lcd_set_brightness(80);
}
