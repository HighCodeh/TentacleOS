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

#include "gpio_header.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#define HDR_PWM_MODE  LEDC_LOW_SPEED_MODE
#define HDR_PWM_TIMER LEDC_TIMER_1
#define HDR_PWM_CH    LEDC_CHANNEL_1
#define HDR_PWM_RES   LEDC_TIMER_10_BIT
#define HDR_PWM_FREQ  1000
#define HDR_PWM_MAX   1023
#define HDR_DUTY_FULL 100

static const gpio_hdr_info_t HEADER[GPIO_HEADER_COUNT] = {
    {1, -1, GPIO_HDR_POWER, "4.2V", "VBAT rail", "battery, unregulated"},
    {2, 22, GPIO_HDR_SPI_BUS, "GPIO22", "SPI MOSI", "display + radios"},
    {3, 23, GPIO_HDR_SPI_BUS, "GPIO23", "SPI MISO", "display + radios"},
    {4, 33, GPIO_HDR_LOCKED, "GPIO33", "Charger CE", "BQ25896 charge enable"},
    {5, 21, GPIO_HDR_SPI_BUS, "GPIO21", "SPI SCLK", "display + radios"},
    {6, 12, GPIO_HDR_FREE, "GPIO12", "Free GPIO", "no on-board owner"},
    {7, 11, GPIO_HDR_PERIPH, "GPIO11", "RFID LF", "125 kHz RFID (idle)"},
    {8, -1, GPIO_HDR_GND, "GND", "Ground", ""},
    {9, -1, GPIO_HDR_POWER, "3.3V", "3V3 rail", "regulated, shared"},
    {10, 5, GPIO_HDR_PERIPH, "GPIO5", "LoRa DIO1", "SX1262 IRQ"},
    {11, -1, GPIO_HDR_GND, "GND", "Ground", ""},
    {12, 4, GPIO_HDR_PERIPH, "GPIO4", "LoRa BUSY", "SX1262 busy"},
    {13, 37, GPIO_HDR_CONSOLE, "GPIO37", "UART0 RX", "console + P4 strap"},
    {14, 38, GPIO_HDR_CONSOLE, "GPIO38", "UART0 TX", "console + P4 strap"},
    {15, 30, GPIO_HDR_I2C_BUS, "GPIO30", "I2C SCL", "charger/gauge/LED/haptic"},
    {16, 31, GPIO_HDR_I2C_BUS, "GPIO31", "I2C SDA", "charger/gauge/LED/haptic"},
    {17, 19, GPIO_HDR_LOCKED, "GPIO19", "USB mux", "TS3USB221 D+/D- select"},
    {18, -1, GPIO_HDR_GND, "GND", "Ground", ""},
};

static gpio_hdr_mode_t s_mode[GPIO_HEADER_COUNT];
static bool s_out_level[GPIO_HEADER_COUNT];
static uint8_t s_duty[GPIO_HEADER_COUNT];
static bool s_is_changed[GPIO_HEADER_COUNT];
static bool s_is_pwm_ready;

static bool idx_ok(int idx) {
  return idx >= 0 && idx < GPIO_HEADER_COUNT;
}

static void configure_input(int gpio) {
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << gpio,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cfg);
}

static void configure_output(int gpio) {
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << gpio,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cfg);
}

static void pwm_ready(void) {
  if (s_is_pwm_ready)
    return;
  ledc_timer_config_t tc = {
      .speed_mode = HDR_PWM_MODE,
      .timer_num = HDR_PWM_TIMER,
      .duty_resolution = HDR_PWM_RES,
      .freq_hz = HDR_PWM_FREQ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&tc);
  s_is_pwm_ready = true;
}

static void pwm_start(int gpio, uint8_t duty_pct) {
  pwm_ready();
  ledc_channel_config_t cc = {
      .gpio_num = gpio,
      .speed_mode = HDR_PWM_MODE,
      .channel = HDR_PWM_CH,
      .timer_sel = HDR_PWM_TIMER,
      .duty = (uint32_t)duty_pct * HDR_PWM_MAX / HDR_DUTY_FULL,
      .hpoint = 0,
  };
  ledc_channel_config(&cc);
}

static void pwm_stop(void) {
  if (s_is_pwm_ready)
    ledc_stop(HDR_PWM_MODE, HDR_PWM_CH, 0);
}

void gpio_header_begin(void) {
  for (int i = 0; i < GPIO_HEADER_COUNT; i++) {
    s_mode[i] = GPIO_HDR_MODE_INPUT;
    s_out_level[i] = false;
    s_duty[i] = 0;
    s_is_changed[i] = false;
  }
}

int gpio_header_count(void) {
  return GPIO_HEADER_COUNT;
}

const gpio_hdr_info_t *gpio_header_info(int idx) {
  return idx_ok(idx) ? &HEADER[idx] : NULL;
}

bool gpio_header_is_actionable(int idx) {
  if (!idx_ok(idx) || HEADER[idx].gpio < 0)
    return false;
  return HEADER[idx].klass != GPIO_HDR_LOCKED && HEADER[idx].klass != GPIO_HDR_CONSOLE;
}

gpio_hdr_mode_t gpio_header_mode(int idx) {
  return idx_ok(idx) ? s_mode[idx] : GPIO_HDR_MODE_INPUT;
}

esp_err_t gpio_header_set_mode(int idx, gpio_hdr_mode_t mode) {
  if (!gpio_header_is_actionable(idx))
    return ESP_ERR_NOT_ALLOWED;
  int gpio = HEADER[idx].gpio;

  if (s_mode[idx] == GPIO_HDR_MODE_PWM && mode != GPIO_HDR_MODE_PWM)
    pwm_stop();

  switch (mode) {
    case GPIO_HDR_MODE_OUTPUT:
      configure_output(gpio);
      gpio_set_level(gpio, s_out_level[idx]);
      break;
    case GPIO_HDR_MODE_PWM:
      pwm_start(gpio, s_duty[idx]);
      break;
    case GPIO_HDR_MODE_INPUT:
    case GPIO_HDR_MODE_HIZ:
    default:
      configure_input(gpio);
      break;
  }
  s_mode[idx] = mode;
  s_is_changed[idx] = true;
  return ESP_OK;
}

esp_err_t gpio_header_write(int idx, bool level) {
  if (!gpio_header_is_actionable(idx))
    return ESP_ERR_NOT_ALLOWED;
  if (s_mode[idx] != GPIO_HDR_MODE_OUTPUT)
    return ESP_ERR_INVALID_STATE;
  gpio_set_level(HEADER[idx].gpio, level);
  s_out_level[idx] = level;
  s_is_changed[idx] = true;
  return ESP_OK;
}

bool gpio_header_level(int idx) {
  return idx_ok(idx) ? s_out_level[idx] : false;
}

int gpio_header_read(int idx) {
  if (!gpio_header_is_actionable(idx))
    return -1;
  if (s_mode[idx] == GPIO_HDR_MODE_PWM)
    return -1;
  return gpio_get_level(HEADER[idx].gpio);
}

esp_err_t gpio_header_pwm_duty(int idx, uint8_t duty_pct) {
  if (!gpio_header_is_actionable(idx) || s_mode[idx] != GPIO_HDR_MODE_PWM)
    return ESP_ERR_INVALID_STATE;
  if (duty_pct > HDR_DUTY_FULL)
    duty_pct = HDR_DUTY_FULL;
  s_duty[idx] = duty_pct;
  ledc_set_duty(HDR_PWM_MODE, HDR_PWM_CH, (uint32_t)duty_pct * HDR_PWM_MAX / HDR_DUTY_FULL);
  ledc_update_duty(HDR_PWM_MODE, HDR_PWM_CH);
  return ESP_OK;
}

uint8_t gpio_header_pwm_get(int idx) {
  return idx_ok(idx) ? s_duty[idx] : 0;
}

void gpio_header_restore_all(void) {
  pwm_stop();
  for (int i = 0; i < GPIO_HEADER_COUNT; i++) {
    if (!s_is_changed[i])
      continue;
    if (HEADER[i].gpio >= 0)
      configure_input(HEADER[i].gpio);
    s_mode[i] = GPIO_HDR_MODE_INPUT;
    s_out_level[i] = false;
    s_duty[i] = 0;
    s_is_changed[i] = false;
  }
}
