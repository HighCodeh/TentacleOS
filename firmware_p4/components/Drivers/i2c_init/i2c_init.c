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

#include "driver/i2c.h"
#include "esp_log.h"

#include "pin_def.h"

static const char *TAG = "I2C_INIT";

// 100 kHz standard-mode. The V2 board pulls SDA/SCL up with 10k (R12/R13/R14 to
// 3.3VF) plus 22R series (R9/R10) — too weak for 400 kHz fast-mode (rise time
// can't reach VIH in a bit period), which is why the BQ25896 NACKs at 400 kHz.
#define I2C_MASTER_FREQ_HZ 100000

void init_i2c(void) {
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
    return;
  }

  ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install I2C driver: %s", esp_err_to_name(ret));
    return;
  }

  ESP_LOGI(TAG, "I2C master initialized on I2C_NUM_0");
}
