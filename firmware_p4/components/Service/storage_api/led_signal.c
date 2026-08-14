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

#include "led_signal.h"

#include "esp_timer.h"

#include "led_control.h"
#include "tos_config.h"

// A signal is a single short flash, then the LED goes dark again - the status
// LED is normally off and only blinks on events. The off is scheduled on a
// one-shot esp_timer so the caller never blocks (safe from any task/context).
#define LED_BLINK_US (150 * 1000)

static esp_timer_handle_t s_off_timer = NULL;

static void led_off_cb(void *arg) {
  (void)arg;
  led_clear();
}

static void blink(uint32_t hex) {
  int pct = g_config_led.brightness;
  uint8_t r = (uint8_t)(((hex >> 16) & 0xFF) * (uint32_t)pct / 100);
  uint8_t g = (uint8_t)(((hex >> 8) & 0xFF) * (uint32_t)pct / 100);
  uint8_t b = (uint8_t)((hex & 0xFF) * (uint32_t)pct / 100);
  led_set_color(r, g, b);

  if (s_off_timer == NULL) {
    const esp_timer_create_args_t args = {.callback = led_off_cb, .name = "led_off"};
    if (esp_timer_create(&args, &s_off_timer) != ESP_OK) {
      s_off_timer = NULL;
    }
  }
  if (s_off_timer != NULL) {
    esp_timer_stop(s_off_timer); // re-arm if a previous flash is still pending
    esp_timer_start_once(s_off_timer, LED_BLINK_US);
  }
}

void led_signal_info(void) {
  blink(g_config_led.info_color);
}

void led_signal_warning(void) {
  blink(g_config_led.warning_color);
}

void led_signal_error(void) {
  blink(g_config_led.error_color);
}
