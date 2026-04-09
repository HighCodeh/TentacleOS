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

#include "i2c_init.h"

#include "driver/i2c.h"
#include "esp_log.h"

#include "pin_def.h"

static const char *TAG = "I2C_INIT";

#define I2C_MASTER_FREQ_HZ 400000

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
