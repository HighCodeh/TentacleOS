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

#include "led_control.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#include "pin_def.h"

static const char *TAG = "LED_CONTROL";

#define LED_RMT_RESOLUTION_HZ (10 * 1000 * 1000) // 10 MHz

#define BLINK_RED_MS    500
#define BLINK_GREEN_MS  220
#define BLINK_BLUE_MS   500
#define BLINK_PURPLE_MS 500

static led_strip_handle_t s_led_strip = NULL;

esp_err_t led_rgb_init(void) {
  if (GPIO_LED_RGB_PIN < 0) {
    // V2 drives the RGB LED through the LP5816 I2C driver, not a WS2812 on a GPIO.
    return ESP_OK;
  }

  led_strip_config_t strip_config = {
      .strip_gpio_num = GPIO_LED_RGB_PIN,
      .max_leds = LED_COUNT,
  };

  led_strip_rmt_config_t rmt_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = LED_RMT_RESOLUTION_HZ,
      .flags.with_dma = false,
  };

  esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
    return err;
  }

  led_strip_clear(s_led_strip);
  ESP_LOGI(TAG, "RGB LED initialized on GPIO %d", GPIO_LED_RGB_PIN);

  return ESP_OK;
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
  if (s_led_strip == NULL) {
    return;
  }
  led_strip_set_pixel(s_led_strip, 0, r, g, b);
  led_strip_refresh(s_led_strip);
}

void led_clear(void) {
  if (s_led_strip == NULL) {
    return;
  }
  led_strip_clear(s_led_strip);
  led_strip_refresh(s_led_strip);
}

void led_blink(uint8_t r, uint8_t g, uint8_t b, int duration_ms) {
  led_set_color(r, g, b);
  vTaskDelay(pdMS_TO_TICKS(duration_ms));
  led_clear();
}

void led_blink_red(void) {
  led_blink(255, 0, 0, BLINK_RED_MS);
}

void led_blink_green(void) {
  led_blink(0, 150, 0, BLINK_GREEN_MS);
}

void led_blink_blue(void) {
  led_blink(0, 0, 255, BLINK_BLUE_MS);
}

void led_blink_purple(void) {
  led_blink(200, 0, 220, BLINK_PURPLE_MS);
}
