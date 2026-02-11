#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "st7789.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/spi_types.h"
#include "pin_def.h"
#include "spi.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "storage_assets.h"
#include "cJSON.h"

#define BL_LEDC_TIMER     LEDC_TIMER_0
#define BL_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BL_LEDC_CH        LEDC_CHANNEL_0
#define BL_LEDC_RES       LEDC_TIMER_13_BIT
#define BL_LEDC_FREQ      5000
#define DISPLAY_CONFIG_PATH "/assets/config/screen/screen_config.conf"

static const char *TAG = "ST7789_DRIVER";

esp_lcd_panel_io_handle_t io_handle = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;

typedef struct {
    uint8_t brightness;
    uint8_t rotation;
} display_config_t;

static void init_backlight_pwm(void)
{
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
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static display_config_t load_display_config(void)
{
    display_config_t cfg = {.brightness = 80, .rotation = 1};
    if (storage_assets_is_mounted()) {
        FILE *f = fopen(DISPLAY_CONFIG_PATH, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);

            char *data = malloc(fsize + 1);
            if (data) {
                fread(data, 1, fsize, f);
                data[fsize] = 0;
                cJSON *json = cJSON_Parse(data);
                if (json) {
                    cJSON *br = cJSON_GetObjectItem(json, "brightness");
                    cJSON *rt = cJSON_GetObjectItem(json, "rotation");
                    if (cJSON_IsNumber(br)) cfg.brightness = br->valueint;
                    if (cJSON_IsNumber(rt)) cfg.rotation = rt->valueint;
                    cJSON_Delete(json);
                }
                free(data);
            }
            fclose(f);
        }
    }
    return cfg;
}

static void save_display_config(display_config_t cfg)
{
    if (storage_assets_is_mounted()) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "brightness", cfg.brightness);
        cJSON_AddNumberToObject(json, "rotation", cfg.rotation);
        char *data = cJSON_PrintUnformatted(json);
        
        FILE *f = fopen(DISPLAY_CONFIG_PATH, "w");
        if (f) {
            fputs(data, f);
            fclose(f);
        }
        cJSON_free(data);
        cJSON_Delete(json);
    }
}

void lcd_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    display_config_t cfg = load_display_config();
    cfg.brightness = percent;
    uint32_t duty = (8191 * percent) / 100;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CH, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CH);
    save_display_config(cfg);
}

uint8_t lcd_get_brightness(void)
{
    return load_display_config().brightness;
}

void lcd_set_rotation(uint8_t rotation)
{
    if (rotation < 1) rotation = 1;
    if (rotation > 4) rotation = 4;
    display_config_t cfg = load_display_config();
    cfg.rotation = rotation;

    switch (rotation) {
        case 1:
            esp_lcd_panel_mirror(panel_handle, false, false);
            esp_lcd_panel_swap_xy(panel_handle, false);
            esp_lcd_panel_set_gap(panel_handle, 0, 0);
            break;
        case 2:
            esp_lcd_panel_mirror(panel_handle, true, false);
            esp_lcd_panel_swap_xy(panel_handle, true);
            esp_lcd_panel_set_gap(panel_handle, 0, 0);
            break;
        case 3:
            esp_lcd_panel_mirror(panel_handle, true, true);
            esp_lcd_panel_swap_xy(panel_handle, false);
            esp_lcd_panel_set_gap(panel_handle, 0, 80); 
            break;
        case 4:
            esp_lcd_panel_mirror(panel_handle, false, true);
            esp_lcd_panel_swap_xy(panel_handle, true);
            esp_lcd_panel_set_gap(panel_handle, 80, 0);
            break;
    }
    save_display_config(cfg);
}

uint8_t lcd_get_rotation(void)
{
    return load_display_config().rotation;
}

void lcd_clear_screen(uint16_t color) {
    uint16_t buffer[LCD_H_RES];
    for (int i = 0; i < LCD_H_RES; i++) buffer[i] = color;
    for (int y = 0; y < LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_H_RES, y+1, buffer);
    }
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
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    init_backlight_pwm();
    display_config_t saved_cfg = load_display_config();
    lcd_set_brightness(saved_cfg.brightness);
    lcd_set_rotation(saved_cfg.rotation);
    lcd_clear_screen(0x0000);
    vTaskDelay(pdMS_TO_TICKS(100));
}