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

#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_init.h"
#include "sys_prio.h"

#define LP5816_ADDR              0x2C
#define I2C_TIMEOUT_MS           50
#define REG_OUT0_PWM             0x18
#define SIGNAL_BLINK_US          350000
#define LED_TICK_MS              100
#define LED_ACTIVE_TICK_MS       5
#define LED_ALERT_PERIOD_MS      2400
#define LED_ALERT_ON_MS          300
#define LED_STACK_BYTES          3072
#define LED_ACTIVITY_PERIOD_MS   100
#define LED_ACTIVITY_ON_MS       10
#define LED_SUBGHZ_PULSE_US      100000
#define LED_RGB_MASK             0xFFFFFF
#define LED_COLOR_CHARGING       0xFF1800
#define LED_COLOR_FULL           0x00FF30
#define LED_CHARGE_PERIOD_US     10000000
#define LED_CHARGE_ON_US         200000
#define LED_FULL_ON_US           1000000
#define LED_POWER_BRIGHTNESS_MAX 10

typedef enum { SIG_INFO, SIG_WARNING, SIG_ERROR, SIG_COUNT } led_signal_t;

static const char *TAG = "LED_CONTROL";
static i2c_master_dev_handle_t s_dev;
static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_colors[SIG_COUNT] = {0xFF00FF, 0xFFFF00, 0xFF0000};
static int s_brightness = 10;
static led_power_state_t s_power;
static led_signal_t s_signal;
static int64_t s_signal_until;
static bool s_manual;
static uint32_t s_manual_color;
static uint32_t s_activity;
static int64_t s_subghz_started_us;
static int64_t s_subghz_until_us;
static bool s_display_active = true;
static int64_t s_charge_pulse_started_us;
static int64_t s_full_until_us;

static esp_err_t write_register(uint8_t reg, uint8_t value) {
  const uint8_t bytes[] = {reg, value};
  return i2c_master_transmit(s_dev, bytes, sizeof(bytes), I2C_TIMEOUT_MS);
}

static uint32_t scale_color(uint32_t color, int brightness) {
  uint32_t result = 0;
  for (int shift = 0; shift <= 16; shift += 8)
    result |= (((color >> shift) & 0xFF) * (uint32_t)brightness / 100) << shift;
  return result;
}

static uint32_t desired_color(int64_t now) {
  uint32_t color = 0;
  portENTER_CRITICAL(&s_lock);
  if (now < s_signal_until)
    color = scale_color(s_colors[s_signal], s_brightness);
  else if (s_manual)
    color = s_manual_color;
  else if (s_activity || now < s_subghz_until_us) {
    int64_t phase = (s_activity & LED_ACTIVITY_SUBGHZ) || now < s_subghz_until_us
                        ? now - s_subghz_started_us
                        : now;
    if ((phase / 1000) % LED_ACTIVITY_PERIOD_MS < LED_ACTIVITY_ON_MS)
      color = scale_color(s_colors[SIG_INFO], s_brightness);
  } else {
    int brightness = s_brightness;
    if ((s_power == LED_POWER_CHARGING || s_power == LED_POWER_FULL) &&
        brightness > LED_POWER_BRIGHTNESS_MAX)
      brightness = LED_POWER_BRIGHTNESS_MAX;
    switch (s_power) {
      case LED_POWER_CHARGING:
        if (!s_display_active && now >= s_charge_pulse_started_us &&
            (now - s_charge_pulse_started_us) % LED_CHARGE_PERIOD_US < LED_CHARGE_ON_US)
          color = LED_COLOR_CHARGING;
        break;
      case LED_POWER_FULL:
        if (now < s_full_until_us)
          color = LED_COLOR_FULL;
        break;
      case LED_POWER_BLOCKED:
      case LED_POWER_LOW:
        if ((now / 1000) % LED_ALERT_PERIOD_MS < LED_ALERT_ON_MS)
          color = s_colors[s_power == LED_POWER_LOW ? SIG_ERROR : SIG_WARNING];
        break;
      default:
        break;
    }
    color = scale_color(color, brightness);
  }
  portEXIT_CRITICAL(&s_lock);
  return color;
}

static void led_task(void *arg) {
  (void)arg;
  uint32_t previous = 0;
  esp_err_t previous_error = ESP_OK;
  for (;;) {
    uint32_t color = desired_color(esp_timer_get_time());
    if (color != previous || previous_error != ESP_OK) {
      const uint8_t bytes[] = {REG_OUT0_PWM, color >> 16, color >> 8, color};
      esp_err_t result = i2c_master_transmit(s_dev, bytes, sizeof(bytes), I2C_TIMEOUT_MS);
      if (result != previous_error) {
        if (result != ESP_OK)
          ESP_LOGW(TAG, "RGB update failed: %s", esp_err_to_name(result));
        else
          ESP_LOGI(TAG, "RGB updates recovered");
      }
      if (result == ESP_OK)
        previous = color;
      previous_error = result;
    }
    portENTER_CRITICAL(&s_lock);
    bool active = s_activity != 0 || esp_timer_get_time() < s_subghz_until_us;
    portEXIT_CRITICAL(&s_lock);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(active ? LED_ACTIVE_TICK_MS : LED_TICK_MS));
  }
}

esp_err_t led_rgb_init(void) {
  if (s_task != NULL)
    return ESP_OK;
  if (s_dev == NULL) {
    const i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                        .device_address = LP5816_ADDR,
                                        .scl_speed_hz = I2C_MASTER_FREQ_HZ};
    esp_err_t result = i2c_master_bus_add_device(i2c_get_bus(), &config, &s_dev);
    if (result != ESP_OK)
      return result;
  }
  esp_err_t result = write_register(0x0E, 0xCC);
  if (result != ESP_OK)
    return result;
  vTaskDelay(pdMS_TO_TICKS(2));
  result = write_register(0x00, 0x01);
  if (result != ESP_OK)
    return result;
  vTaskDelay(pdMS_TO_TICKS(1));
  // Manual PWM, RGB only, 25.5 mA ceiling. OUT3 remains disabled.
  static const uint8_t CONFIG[][2] = {{0x01, 0},
                                      {0x02, 7},
                                      {0x03, 0},
                                      {0x04, 0},
                                      {0x0F, 0x55},
                                      {0x14, 0xFF},
                                      {0x15, 0xFF},
                                      {0x16, 0xFF},
                                      {0x18, 0},
                                      {0x19, 0},
                                      {0x1A, 0}};
  for (size_t i = 0; i < sizeof(CONFIG) / sizeof(CONFIG[0]); ++i) {
    result = write_register(CONFIG[i][0], CONFIG[i][1]);
    if (result != ESP_OK)
      return result;
  }
  if (xTaskCreatePinnedToCore(led_task,
                              "led_sig",
                              LED_STACK_BYTES,
                              NULL,
                              SYS_PRIO_BACKGROUND,
                              &s_task,
                              SYS_CORE_RADIO) != pdPASS) {
    s_task = NULL;
    ESP_LOGE(TAG, "LED worker allocation failed");
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "RGB ready; charge status and prioritized notifications");
  return ESP_OK;
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
  if (xPortInIsrContext())
    return;
  portENTER_CRITICAL(&s_lock);
  s_manual_color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  s_manual = s_manual_color != 0;
  portEXIT_CRITICAL(&s_lock);
  if (s_task != NULL)
    xTaskNotifyGive(s_task);
}

void led_clear(void) {
  led_set_color(0, 0, 0);
}
void led_blink(uint8_t r, uint8_t g, uint8_t b, int duration_ms) {
  if (duration_ms <= 0 || xPortInIsrContext())
    return;
  led_set_color(r, g, b);
  vTaskDelay(pdMS_TO_TICKS(duration_ms));
  led_clear();
}
void led_blink_red(void) {
  led_blink(255, 0, 0, 500);
}
void led_blink_green(void) {
  led_blink(0, 150, 0, 220);
}
void led_blink_blue(void) {
  led_blink(0, 0, 255, 500);
}
void led_blink_purple(void) {
  led_blink(200, 0, 220, 500);
}

void led_set_signal_config(uint32_t info, uint32_t warning, uint32_t error, int brightness) {
  if (brightness < 0)
    brightness = 0;
  if (brightness > 100)
    brightness = 100;
  portENTER_CRITICAL(&s_lock);
  s_colors[SIG_INFO] = info & LED_RGB_MASK;
  s_colors[SIG_WARNING] = warning & LED_RGB_MASK;
  s_colors[SIG_ERROR] = error & LED_RGB_MASK;
  s_brightness = brightness;
  portEXIT_CRITICAL(&s_lock);
  if (s_task != NULL)
    xTaskNotifyGive(s_task);
}

void led_set_power_state(led_power_state_t state) {
  if (state < LED_POWER_OFF || state >= LED_POWER_COUNT || xPortInIsrContext())
    return;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  bool has_changed = s_power != state;
  if (has_changed) {
    s_power = state;
    s_charge_pulse_started_us = now;
    s_full_until_us = state == LED_POWER_FULL ? now + LED_FULL_ON_US : 0;
  }
  portEXIT_CRITICAL(&s_lock);
  if (has_changed && s_task != NULL)
    xTaskNotifyGive(s_task);
}

void led_set_display_active(bool is_active) {
  if (xPortInIsrContext())
    return;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  bool has_changed = s_display_active != is_active;
  if (has_changed) {
    s_display_active = is_active;
    if (!is_active && s_power == LED_POWER_CHARGING)
      s_charge_pulse_started_us = now;
  }
  portEXIT_CRITICAL(&s_lock);
  if (has_changed && s_task != NULL)
    xTaskNotifyGive(s_task);
}

static void signal_event(led_signal_t signal) {
  if (xPortInIsrContext())
    return;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  // Repeated traffic cannot stretch a pulse indefinitely or hide an error.
  if (now >= s_signal_until || signal > s_signal) {
    s_signal = signal;
    s_signal_until = now + SIGNAL_BLINK_US;
  }
  portEXIT_CRITICAL(&s_lock);
  if (s_task != NULL)
    xTaskNotifyGive(s_task);
}
void led_signal_info(void) {
  signal_event(SIG_INFO);
}
void led_signal_warning(void) {
  signal_event(SIG_WARNING);
}
void led_signal_error(void) {
  signal_event(SIG_ERROR);
}

void led_set_activity(uint32_t source, bool active) {
  if (xPortInIsrContext() || source == 0)
    return;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  if (active && (source & LED_ACTIVITY_SUBGHZ)) {
    if (!(s_activity & LED_ACTIVITY_SUBGHZ) && now >= s_subghz_until_us)
      s_subghz_started_us = now;
    s_subghz_until_us = now + LED_SUBGHZ_PULSE_US;
  }
  if (active)
    s_activity |= source;
  else
    s_activity &= ~source;
  portEXIT_CRITICAL(&s_lock);
  if (s_task != NULL)
    xTaskNotifyGive(s_task);
}

esp_err_t led_get_status(led_status_t *out) {
  if (out == NULL)
    return ESP_ERR_INVALID_ARG;
  if (s_task == NULL || xPortInIsrContext())
    return ESP_ERR_INVALID_STATE;
  portENTER_CRITICAL(&s_lock);
  out->activity = s_activity;
  out->brightness = s_brightness;
  out->power = s_power;
  portEXIT_CRITICAL(&s_lock);
  uint8_t reg = REG_OUT0_PWM, rgb[3];
  esp_err_t result = i2c_master_transmit_receive(s_dev, &reg, 1, rgb, sizeof(rgb), I2C_TIMEOUT_MS);
  if (result == ESP_OK)
    out->rgb = ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | rgb[2];
  return result;
}
