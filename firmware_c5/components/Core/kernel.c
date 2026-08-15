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

#include "kernel.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "buttons_gpio.h"
#include "c5_log.h"
#include "i2c_init.h"
#include "led_control.h"
#include "ota_service.h"
#include "pin_def.h"
#include "spi_bridge.h"
#include "storage_assets.h"
#include "sys_monitor.h"
#include "wifi_service.h"

static const char *TAG = "SAFEGUARD";

#define BOOT_SETTLE_MS 1500

void kernel_init(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  init_i2c();
  // Only the assets partition is mounted: it holds P4-editable config (AP name,
  // password, captive-portal HTML, chat config, known networks). Captured data
  // (passwords, pcaps, scans) is NOT persisted on the C5 - it streams to the P4
  // over SPI, which owns storage. So there is no runtime `storage` partition and
  // no storage_init() here.
  storage_assets_init();
  storage_assets_print_info();

  // The BQ25896 charger and RGB LED live on the P4 and are managed there; the C5
  // has no battery/charger driver at all (it is not on the C5's I2C bus).
  // V2 PCB has no bridge IRQ trace: run the slave in POLL mode (matches the P4).
  spi_bridge_slave_init_mode(SPI_BRIDGE_MODE_POLL);
  c5_log_init(); // tee C5 logs to the P4 over SPI for the companion console
  // OTA is triggered on demand over SPI (SPI_ID_SYSTEM_START_UART_OTA); UART0 is
  // the console until then. No always-on UART receiver here anymore.

  sys_monitor_start(false);

  wifi_service_init();

  vTaskDelay(pdMS_TO_TICKS(BOOT_SETTLE_MS));
}

// Safeguards

void safeguard_alert(const char *title, const char *message) {
  ESP_LOGE(TAG, "ALERT: %s - %s", title, message);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  ESP_LOGE(TAG, "!!! CRITICAL STACK OVERFLOW DETECTED !!!");
  ESP_LOGE(TAG, "Task Name: [%s]", pcTaskName);
  ESP_LOGE(TAG, "Task attempted to use more memory than was allocated.");
}

void vApplicationMallocFailedHook(void) {
  ESP_LOGE(TAG, "!!! OUT OF MEMORY (MALLOC FAILED) !!!");
}
