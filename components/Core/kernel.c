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


#include <stdio.h>
#include "buttons_gpio.h"
#include "cc1101.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "buzzer.h"
#include "spi.h"
#include "i2c_init.h"
#include "led_control.h"
#include "pin_def.h" 
#include "st7789.h"
#include "bq25896.h"
#include "driver/i2c.h"
#include "nvs_flash.h" 
#include "wifi_service.h" 
#include "storage_init.h"
#include "storage_assets.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "ram_monitor.h"

void kernel_init(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  spi_init();
  init_i2c();
  //Storage Init
  storage_init();
  storage_assets_init();
  storage_assets_print_info();

  buzzer_init();
  led_rgb_init();
  buzzer_boot_sequence();
  bq25896_init();
  cc1101_init();

  buttons_init();


  // display and graphical api init
  st7789_init();
  lv_init();
  lv_port_disp_init();
  lv_port_indev_init();

  wifi_init();
  wifi_stop();

  ui_init();
  // ram_monitor();

  vTaskDelay(pdMS_TO_TICKS(1500));
}
