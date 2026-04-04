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

#include "kernel.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "spi.h"
#include "i2c_init.h"
#include "st7789.h"
#include "cc1101.h"
#include "bq25896.h"
#include "led_control.h"
#include "buttons_gpio.h"
#include "bridge_manager.h"
#include "storage_init.h"
#include "storage_assets.h"
#include "tos_first_boot.h"
#include "tos_config.h"
#include "tos_theme.h"
#include "tos_log.h"
#include "wifi_service.h"
#include "console_service.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "msgbox_ui.h"
#include "sys_monitor.h"

static const char *TAG = "KERNEL";

#define CONSOLE_TASK_STACK 4096
#define CONSOLE_TASK_PRIO  5
#define BOOT_SETTLE_MS     1500

static void console_task(void *pvParameters) {
  console_service_init();
  vTaskDelete(NULL);
}

void kernel_init(void) {
  // 1. NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // 2. Buses
  spi_init();
  init_i2c();

  // 3. Storage
  storage_init();
  storage_assets_init();
  storage_assets_print_info();

  // 4. Configuration, theme, logging
  tos_first_boot_setup();
  tos_config_load_all();
  tos_log_init();
  tos_theme_load_from_sd();
  ESP_LOGI(TAG, "TentacleOS booted successfully");

  // 5. Peripherals
  led_rgb_init();
  bq25896_init();
  cc1101_init();
  bridge_manager_init();
  buttons_init();

  // 6. Display + LVGL + UI
  st7789_init();
  lv_init();
  lv_port_disp_init();
  lv_port_indev_init();
  ui_init();

  // 7. Services
  sys_monitor_start(false);
  wifi_init();
  xTaskCreate(console_task, "console_task", CONSOLE_TASK_STACK, NULL, CONSOLE_TASK_PRIO, NULL);

  vTaskDelay(pdMS_TO_TICKS(BOOT_SETTLE_MS));
}

// FreeRTOS Safeguards

void safeguard_alert(const char *title, const char *message) {
  ESP_LOGE(TAG, "ALERT: %s - %s", title, message);

  if (ui_acquire()) {
    msgbox_open(LV_SYMBOL_WARNING, message, "OK", NULL, NULL);
    ui_release();
  }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  ESP_LOGE(TAG, "STACK OVERFLOW in task [%s]", pcTaskName);
}

void vApplicationMallocFailedHook(void) {
  ESP_LOGE(TAG, "MALLOC FAILED — out of memory");
}
