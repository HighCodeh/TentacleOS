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
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "pin_def.h"

static const char *TAG = "I2C_INIT";

static i2c_master_bus_handle_t s_bus = NULL;
static uint32_t s_recover_count = 0;

#define I2C_RECOVER_CLOCKS  9 // enough SCL pulses to walk any slave past its ACK
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

  i2c_master_bus_config_t conf = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = GPIO_I2C_SDA_PIN,
      .scl_io_num = GPIO_I2C_SCL_PIN,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };

  esp_err_t ret = i2c_new_master_bus(&conf, &s_bus);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "I2C master bus ready on I2C_NUM_0");
  return ESP_OK;
}

i2c_master_bus_handle_t i2c_get_bus(void) {
  return s_bus;
}

esp_err_t i2c_bus_recover(void) {
  if (s_bus == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  s_recover_count++;
  ESP_LOGW(TAG, "I2C bus reset (recovery #%lu)", (unsigned long)s_recover_count);
  return i2c_master_bus_reset(s_bus);
}

uint32_t i2c_recover_count(void) {
  return s_recover_count;
}
