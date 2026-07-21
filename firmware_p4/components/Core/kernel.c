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
#include "ys_rfid2.h"
#include "bridge_manager.h"
#include "spi_bridge.h"
#include "storage_init.h"
#include "storage_assets.h"
#include "tos_first_boot.h"
#include "tos_config.h"
#include "tos_theme.h"
#include "tos_log.h"
#include "wifi_service.h"
#include "console_service.h"
#include "host_link.h"
#include "host_link_ble.h"
#include "host_link_state.h"
#include "host_link_stream.h"
#include "lvgl_glue.h"
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
  // C5 radio coprocessor disabled: the C5 on this board is unresponsive, so the
  // P4 runs standalone (terminal only). Skip the bridge entirely (no SPI init,
  // no link monitor, no version probe) and mark it permanently dead so any
  // stray send_command short-circuits instead of spamming timeouts. Re-enable
  // bridge_manager_init() (and the host_link C5 pieces below) with a working C5.
  // bridge_manager_init();
  spi_bridge_set_alive(false);
  buttons_init();
  ys_rfid2_init(NULL);

  // 6. Display + LVGL + UI. st7789_init sets up the panel handles; lvgl_glue_init
  // brings LVGL up over esp_lvgl_port (it calls lv_init and registers the
  // display); then the keypad indev and UI lock against the glue.
  st7789_init();
  lvgl_glue_init();
  lv_port_indev_init();
  ui_init();

  // 7. Services
  sys_monitor_start(false);
  wifi_service_init();
  xTaskCreate(console_task, "console_task", CONSOLE_TASK_STACK, NULL, CONSOLE_TASK_PRIO, NULL);

  // Companion host link (USB CDC). Bridge must be up first (commands relay to C5).
  host_link_state_init();  // load toggle settings before the link comes up
  host_link_stream_init(); // streaming + heartbeat proxy state
  host_link_init();
  host_link_cdc_init();
  host_link_log_init();
  // C5-dependent host-link pieces disabled with the bridge: the C5 log relay and
  // the BLE relay status poller (SPI_ID_HOST_STATUS) both talk to the C5.
  // host_link_c5log_init(); // relay C5 logs (SPI_ID_SYSTEM_LOG) as source=C5 LOG frames
  // host_link_ble_init();   // BLE relay infra; advertising starts on demand

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
