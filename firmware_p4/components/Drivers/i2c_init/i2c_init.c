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

#include "i2c_init.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "pin_def.h"

static const char *TAG = "I2C_INIT";

// 100 kHz standard-mode. The V2 board pulls SDA/SCL up with 10k (R12/R13/R14 to
// 3.3VF) plus 22R series (R9/R10) — too weak for 400 kHz fast-mode (rise time
// can't reach VIH in a bit period), which is why the BQ25896 NACKs at 400 kHz.
#define I2C_MASTER_FREQ_HZ 100000

#define I2C_RECOVER_CLOCKS 9  // enough SCL pulses to walk any slave past its ACK
#define I2C_RECOVER_HALF_US 5 // ~100 kHz bit-bang half period

// Clock out a slave that is holding SDA low (stuck mid-transfer after a partial
// reset or power glitch) by bit-banging SCL, then issue a STOP. Runs before the
// I2C driver claims the pins; a no-op when SDA is already released.
static void bus_recover(void) {
  gpio_set_pull_mode(GPIO_I2C_SDA_PIN, GPIO_PULLUP_ONLY);
  gpio_set_direction(GPIO_I2C_SDA_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(GPIO_I2C_SCL_PIN, GPIO_PULLUP_ONLY);
  gpio_set_direction(GPIO_I2C_SCL_PIN, GPIO_MODE_OUTPUT_OD);
  gpio_set_level(GPIO_I2C_SCL_PIN, 1);

  if (gpio_get_level(GPIO_I2C_SDA_PIN) == 1) {
    return;
  }

  ESP_LOGW(TAG, "I2C SDA stuck low; clocking SCL to recover the bus");
  for (int i = 0; i < I2C_RECOVER_CLOCKS && gpio_get_level(GPIO_I2C_SDA_PIN) == 0; i++) {
    gpio_set_level(GPIO_I2C_SCL_PIN, 0);
    esp_rom_delay_us(I2C_RECOVER_HALF_US);
    gpio_set_level(GPIO_I2C_SCL_PIN, 1);
    esp_rom_delay_us(I2C_RECOVER_HALF_US);
  }

  // STOP: SDA rises while SCL is high.
  gpio_set_direction(GPIO_I2C_SDA_PIN, GPIO_MODE_OUTPUT_OD);
  gpio_set_level(GPIO_I2C_SDA_PIN, 0);
  esp_rom_delay_us(I2C_RECOVER_HALF_US);
  gpio_set_level(GPIO_I2C_SCL_PIN, 1);
  esp_rom_delay_us(I2C_RECOVER_HALF_US);
  gpio_set_level(GPIO_I2C_SDA_PIN, 1);
  esp_rom_delay_us(I2C_RECOVER_HALF_US);
}

esp_err_t init_i2c(void) {
  bus_recover();

  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = GPIO_I2C_SDA_PIN,
      .scl_io_num = GPIO_I2C_SCL_PIN,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = I2C_MASTER_FREQ_HZ,
  };

  esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure I2C: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install I2C driver: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "I2C master initialized on I2C_NUM_0");
  return ESP_OK;
}
