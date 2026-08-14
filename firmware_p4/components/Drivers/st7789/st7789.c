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

#include "st7789.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/spi_types.h"

#include "pin_def.h"
#include "storage_assets.h"
#include "tos_flash_paths.h"
#include "cJSON.h"

static const char *TAG = "ST7789";

// Backlight PWM configuration
#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_LEDC_MODE  LEDC_LOW_SPEED_MODE
#define BL_LEDC_CH    LEDC_CHANNEL_0
#define BL_LEDC_RES   LEDC_TIMER_13_BIT
#define BL_LEDC_FREQ  5000
#define BL_MAX_DUTY   8191 // (2^13) - 1

// Display defaults
#define DEFAULT_BRIGHTNESS 80
#define DEFAULT_ROTATION   1
#define MIN_ROTATION       1
#define MAX_ROTATION       4
#define MAX_BRIGHTNESS     100

// Rotation 3/4 gap offset for 240x320 -> 240x240 panel
#define ROTATION_GAP_OFFSET 80

// SPI IO queue depth
#define SPI_IO_QUEUE_DEPTH 10

// Post-init settle time
#define INIT_SETTLE_MS 100

#define DISPLAY_CONFIG_PATH FLASH_CONFIG_SCREEN

esp_lcd_panel_io_handle_t io_handle = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;

typedef struct {
  uint8_t brightness;
  uint8_t rotation;
} display_config_t;

static void init_backlight_pwm(void) {
  ledc_timer_config_t ledc_timer = {
      .speed_mode = BL_LEDC_MODE,
      .timer_num = BL_LEDC_TIMER,
      .duty_resolution = BL_LEDC_RES,
      .freq_hz = BL_LEDC_FREQ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

  ledc_channel_config_t ledc_channel = {
      .speed_mode = BL_LEDC_MODE,
      .channel = BL_LEDC_CH,
      .timer_sel = BL_LEDC_TIMER,
      .intr_type = LEDC_INTR_DISABLE,
      .gpio_num = GPIO_ST7789_BL_PIN,
      .duty = 0,
      .hpoint = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static display_config_t load_display_config(void) {
  display_config_t cfg = {
      .brightness = DEFAULT_BRIGHTNESS,
      .rotation = DEFAULT_ROTATION,
  };

  if (!storage_assets_is_mounted()) {
    return cfg;
  }

  FILE *f = fopen(DISPLAY_CONFIG_PATH, "r");
  if (f == NULL) {
    return cfg;
  }

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *data = malloc(fsize + 1);
  if (data == NULL) {
    fclose(f);
    return cfg;
  }

  fread(data, 1, fsize, f);
  data[fsize] = '\0';
  fclose(f);

  cJSON *json = cJSON_Parse(data);
  if (json != NULL) {
    cJSON *br = cJSON_GetObjectItem(json, "brightness");
    cJSON *rt = cJSON_GetObjectItem(json, "rotation");
    if (cJSON_IsNumber(br)) {
      cfg.brightness = br->valueint;
    }
    if (cJSON_IsNumber(rt)) {
      cfg.rotation = rt->valueint;
    }
    cJSON_Delete(json);
  }

  free(data);
  return cfg;
}

static void save_display_config(display_config_t cfg) {
  if (!storage_assets_is_mounted()) {
    return;
  }

  cJSON *json = cJSON_CreateObject();
  cJSON_AddNumberToObject(json, "brightness", cfg.brightness);
  cJSON_AddNumberToObject(json, "rotation", cfg.rotation);
  char *data = cJSON_PrintUnformatted(json);

  FILE *f = fopen(DISPLAY_CONFIG_PATH, "w");
  if (f != NULL) {
    fputs(data, f);
    fclose(f);
  }

  cJSON_free(data);
  cJSON_Delete(json);
}

void lcd_apply_brightness(uint8_t percent) {
  if (percent > MAX_BRIGHTNESS) {
    percent = MAX_BRIGHTNESS;
  }
  uint32_t duty = (BL_MAX_DUTY * percent) / MAX_BRIGHTNESS;
  ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CH, duty);
  ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CH);
}

void lcd_set_brightness(uint8_t percent) {
  if (percent > MAX_BRIGHTNESS) {
    percent = MAX_BRIGHTNESS;
  }
  lcd_apply_brightness(percent);

  // Persist the user-chosen level. Transient changes (auto-dim) use
  // lcd_apply_brightness so they never overwrite this.
  display_config_t cfg = load_display_config();
  cfg.brightness = percent;
  save_display_config(cfg);
}

void lcd_display_sleep(bool sleep) {
  if (panel_handle == NULL) {
    return;
  }
  // Turn the panel off (sleep) or on. Pair with backlight off/restore for a real
  // screen-off; the panel command cuts the pixels, the backlight cuts the light.
  esp_lcd_panel_disp_on_off(panel_handle, !sleep);
}

uint8_t lcd_get_brightness(void) {
  return load_display_config().brightness;
}

void lcd_set_rotation(uint8_t rotation) {
  if (rotation < MIN_ROTATION) {
    rotation = MIN_ROTATION;
  }
  if (rotation > MAX_ROTATION) {
    rotation = MAX_ROTATION;
  }

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
      esp_lcd_panel_set_gap(panel_handle, 0, ROTATION_GAP_OFFSET);
      break;
    case 4:
      esp_lcd_panel_mirror(panel_handle, false, true);
      esp_lcd_panel_swap_xy(panel_handle, true);
      esp_lcd_panel_set_gap(panel_handle, ROTATION_GAP_OFFSET, 0);
      break;
    default:
      break;
  }

  save_display_config(cfg);
}

uint8_t lcd_get_rotation(void) {
  return load_display_config().rotation;
}

void st7789_fill_screen(uint16_t color) {
  uint16_t buffer[LCD_H_RES];
  for (int i = 0; i < LCD_H_RES; i++) {
    buffer[i] = color;
  }
  for (int y = 0; y < LCD_V_RES; y++) {
    esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_H_RES, y + 1, buffer);
  }
}

void st7789_init(void) {
  esp_lcd_panel_io_spi_config_t io_config = {
      .dc_gpio_num = GPIO_ST7789_DC_PIN,
      .cs_gpio_num = GPIO_ST7789_CS_PIN,
      .pclk_hz = LCD_PIXEL_CLOCK_HZ,
      .lcd_cmd_bits = LCD_CMD_BITS,
      .lcd_param_bits = LCD_PARAM_BITS,
      .spi_mode = 0,
      .trans_queue_depth = SPI_IO_QUEUE_DEPTH,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io_handle));

  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = GPIO_ST7789_RST_PIN,
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
  // Apply only (no re-save): the screen config file is owned/written by
  // tos_config ("screen" module, full schema). Persisting here would drop the
  // auto_lock_seconds / auto_dim / theme fields it does not know about.
  lcd_apply_brightness(saved_cfg.brightness);
  lcd_set_rotation(saved_cfg.rotation);
  st7789_fill_screen(0x0000);

  vTaskDelay(pdMS_TO_TICKS(INIT_SETTLE_MS));

  ESP_LOGI(TAG,
           "Display initialized (%dx%d, brightness: %d%%, rotation: %d)",
           LCD_H_RES,
           LCD_V_RES,
           saved_cfg.brightness,
           saved_cfg.rotation);
}
