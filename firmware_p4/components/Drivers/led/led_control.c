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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "i2c_init.h"

static const char *TAG = "LED_CONTROL";

// V2 RGB LED: Texas Instruments LP5816 4-channel I2C current-sink driver (U22).
// The LEDs are common-anode (anode to 3.3V), the chip sinks each cathode. Channel
// map from the schematic (sheet 6): OUT0 = RED (D10.1), OUT1 = GREEN (D10.2),
// OUT2 = BLUE (D10.3), OUT3 = D11 (unused). I2C target address is 0x2C.
#define LP5816_ADDR 0x2C
#define I2C_TIMEOUT_MS 50

#define REG_CHIP_EN      0x00 // bit0 = device enable
#define REG_DEV_CONFIG0  0x01 // bit0 = MAX_CURRENT (0 = 25.5mA, 1 = 51mA)
#define REG_DEV_CONFIG1  0x02 // bits[3:0] = OUT3..OUT0 enable
#define REG_DEV_CONFIG2  0x03 // fade time + per-channel fade enable
#define REG_DEV_CONFIG3  0x04 // per-channel exponential dimming enable
#define REG_RESET_CMD    0x0E // write 0xCC = reset all registers
#define REG_UPDATE_CMD   0x0F // write 0x55 = latch DEV_CONFIGx
#define REG_OUT0_DC      0x14 // dot-current (analog brightness ceiling) OUT0..OUT3
#define REG_OUT0_PWM     0x18 // 8-bit manual PWM duty OUT0..OUT3

#define CMD_RESET  0xCC
#define CMD_UPDATE 0x55

// Enable OUT0..OUT2 (RGB). OUT3 (D11) is left disabled.
#define OUT_ENABLE_RGB 0x07
// Dot current ceiling per channel: full scale (25.5mA) for vivid, saturated
// color. The signals only flash briefly (~150ms), so peak current is fine.
#define LED_DC_LEVEL 0xFF

static bool s_present = false;

static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t lp5816_write(uint8_t reg, uint8_t val) {
  if (s_dev == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  uint8_t buf[2] = {reg, val};
  return i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

esp_err_t led_rgb_init(void) {
  // Assumes the shared I2C bus (init_i2c) is already up. Reset to a known state,
  // then configure manual PWM mode with OUT0..OUT2 enabled.
  if (s_dev == NULL) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LP5816_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t add = i2c_master_bus_add_device(i2c_get_bus(), &dev_cfg, &s_dev);
    if (add != ESP_OK) {
      s_present = false;
      ESP_LOGE(TAG, "LP5816 add_device failed: %s", esp_err_to_name(add));
      return add;
    }
  }

  esp_err_t err = lp5816_write(REG_RESET_CMD, CMD_RESET);
  if (err != ESP_OK) {
    s_present = false;
    ESP_LOGE(TAG, "LP5816 not responding (0x%02X): %s", LP5816_ADDR, esp_err_to_name(err));
    return err;
  }
  vTaskDelay(pdMS_TO_TICKS(2));

  lp5816_write(REG_CHIP_EN, 0x01);
  vTaskDelay(pdMS_TO_TICKS(1));

  lp5816_write(REG_DEV_CONFIG0, 0x00);           // MAX_CURRENT = 25.5mA
  lp5816_write(REG_DEV_CONFIG1, OUT_ENABLE_RGB); // enable OUT0..OUT2
  lp5816_write(REG_DEV_CONFIG2, 0x00);           // no fade: PWM updates immediately
  lp5816_write(REG_DEV_CONFIG3, 0x00);           // linear dimming
  lp5816_write(REG_UPDATE_CMD, CMD_UPDATE);      // latch the DEV_CONFIG changes

  lp5816_write(REG_OUT0_DC + 0, LED_DC_LEVEL);   // red current ceiling
  lp5816_write(REG_OUT0_DC + 1, LED_DC_LEVEL);   // green
  lp5816_write(REG_OUT0_DC + 2, LED_DC_LEVEL);   // blue

  lp5816_write(REG_OUT0_PWM + 0, 0);             // start dark
  lp5816_write(REG_OUT0_PWM + 1, 0);
  lp5816_write(REG_OUT0_PWM + 2, 0);

  s_present = true;
  ESP_LOGI(TAG, "RGB LED (LP5816) initialized on I2C 0x%02X", LP5816_ADDR);
  return ESP_OK;
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
  if (!s_present) {
    return;
  }
  lp5816_write(REG_OUT0_PWM + 0, r); // OUT0 = red
  lp5816_write(REG_OUT0_PWM + 1, g); // OUT1 = green
  lp5816_write(REG_OUT0_PWM + 2, b); // OUT2 = blue
}

void led_clear(void) {
  led_set_color(0, 0, 0);
}

void led_blink(uint8_t r, uint8_t g, uint8_t b, int duration_ms) {
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

// Semantic status signals: a single short flash in a configurable color, then
// the LED goes dark again (the status LED is normally off). The colors and the
// global brightness come from the LED config, pushed in by the Service layer via
// led_set_signal_config(); the driver keeps sensible defaults until then, so it
// never needs to reach up into the config itself.
#define SIGNAL_BLINK_US (250 * 1000)

enum { SIG_INFO = 0, SIG_WARNING, SIG_ERROR };
static uint32_t s_sig_color[3] = {0xFF00FF, 0xFFFF00, 0xFF0000}; // info, warning, error
static int s_sig_brightness = 10;

// Turning the LED off is a blocking I2C write, so it runs in a dedicated task
// (created in led_rgb_init) rather than an esp_timer callback: the esp_timer task
// has a small stack (~3.5 KB) that the I2C path overflows, and blocking there
// stalls every other timer. sig_blink just sets the color and the off-deadline;
// the task sleeps until the (possibly re-armed) deadline, then clears once.
static volatile int64_t s_sig_off_us = 0;
static TaskHandle_t s_sig_task = NULL;

static void sig_off_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      int64_t remaining = s_sig_off_us - esp_timer_get_time();
      if (remaining <= 0) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS((uint32_t)(remaining / 1000) + 1));
    }
    led_clear();
  }
}

static uint8_t sig_scale(uint8_t chan) {
  int pct = s_sig_brightness;
  if (pct < 0)
    pct = 0;
  if (pct > 100)
    pct = 100;
  return (uint8_t)((uint32_t)chan * (uint32_t)pct / 100);
}

static void sig_blink(uint32_t hex) {
  led_set_color(sig_scale((hex >> 16) & 0xFF), sig_scale((hex >> 8) & 0xFF), sig_scale(hex & 0xFF));
  s_sig_off_us = esp_timer_get_time() + SIGNAL_BLINK_US;

  if (s_sig_task == NULL) {
    // Created lazily on the first signal (all defs are in scope here).
    xTaskCreatePinnedToCore(sig_off_task, "led_sig", 3072, NULL, SYS_PRIO_BACKGROUND, &s_sig_task,
                            SYS_CORE_RADIO);
  }
  if (s_sig_task != NULL) {
    xTaskNotifyGive(s_sig_task); // wake the off task to (re)schedule the clear
  }
}

void led_set_signal_config(uint32_t info, uint32_t warning, uint32_t error, int brightness) {
  s_sig_color[SIG_INFO] = info & 0xFFFFFF;
  s_sig_color[SIG_WARNING] = warning & 0xFFFFFF;
  s_sig_color[SIG_ERROR] = error & 0xFFFFFF;
  s_sig_brightness = brightness;
}

void led_signal_info(void) {
  sig_blink(s_sig_color[SIG_INFO]);
}

void led_signal_warning(void) {
  sig_blink(s_sig_color[SIG_WARNING]);
}

void led_signal_error(void) {
  sig_blink(s_sig_color[SIG_ERROR]);
}
