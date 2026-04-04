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
