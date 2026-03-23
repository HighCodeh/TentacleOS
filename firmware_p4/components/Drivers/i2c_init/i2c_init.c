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


#include "driver/i2c_master.h"
#include "esp_log.h"
#include "i2c_init.h"

#define TAG "I2CInit"

static i2c_master_bus_handle_t global_bus_handle = NULL;

void init_i2c(void) {
    if (global_bus_handle != NULL) {
        ESP_LOGI(TAG, "I2C bus já está inicializado.");
        return;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = 8,
        .scl_io_num = 9,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &global_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao inicializar i2c_new_master_bus: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "I2C mestre inicializado com sucesso no I2C_NUM_0 usando o novo driver.");
    }
}

i2c_master_bus_handle_t i2c_get_bus_handle(void) {
    return global_bus_handle;
}
