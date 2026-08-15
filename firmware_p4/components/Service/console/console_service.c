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

#include "console_service.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/uart.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "linenoise/linenoise.h"
#include "sdkconfig.h"

static const char *TAG = "CONSOLE";

#define CONSOLE_MAX_CMDLINE     512
// REPL task stack: comes from INTERNAL RAM (FreeRTOS stacks can't live in PSRAM).
// 32 KB failed to allocate late in boot (LVGL's full-frame buffer + audio + tasks
// have eaten internal RAM), which made esp_console_new_repl_uart return ESP_FAIL
// and killed the UART console. 8 KB is plenty for the REPL + command handlers.
#define CONSOLE_TASK_STACK_SIZE 8192

static esp_console_repl_t *s_repl = NULL;

esp_err_t console_service_init(void) {
  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

  repl_config.prompt = "highboy> ";
  repl_config.max_cmdline_length = CONSOLE_MAX_CMDLINE;
  repl_config.task_stack_size = CONSOLE_TASK_STACK_SIZE;

  esp_err_t ret = esp_console_register_help_command();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "register help command failed: %s", esp_err_to_name(ret));
    return ret;
  }

  register_system_commands();
  register_fs_commands();
  register_wifi_commands();
  register_badusb_commands();
  register_hostlink_commands();
  register_screen_commands();
  register_ir_commands();
  register_battery_commands();

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
  ESP_LOGI(TAG, "Initializing USB Serial/JTAG Console (Native S3)");
  esp_console_dev_usb_serial_jtag_config_t usbjtag_config =
      ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  ret = esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, &repl);

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
  ESP_LOGI(TAG, "Initializing USB CDC Console (TinyUSB)");
  esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_USB_CDC_CONFIG_DEFAULT();
  ret = esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl);

#else
  ESP_LOGI(TAG, "Initializing UART Console");
  esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  // esp_console_new_repl_uart calls uart_driver_install internally, which returns
  // ESP_FAIL if the driver is already installed on the console UART. Something
  // installs it before us, so free it here and let the REPL install it fresh.
  if (uart_is_driver_installed(CONFIG_ESP_CONSOLE_UART_NUM)) {
    uart_driver_delete(CONFIG_ESP_CONSOLE_UART_NUM);
  }
  ret = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
#endif
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "console REPL create failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_console_start_repl(repl);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "console REPL start failed: %s", esp_err_to_name(ret));
    return ret;
  }
  s_repl = repl;

#if CONFIG_PM_ENABLE && defined(CONFIG_ESP_CONSOLE_UART)
  // With light sleep on, let UART RX wake the CPU so the serial console stays
  // usable while the screen is off. Best-effort: log and continue if unsupported.
  esp_err_t wake = uart_set_wakeup_threshold(CONFIG_ESP_CONSOLE_UART_NUM, 3);
  if (wake == ESP_OK) {
    wake = esp_sleep_enable_uart_wakeup(CONFIG_ESP_CONSOLE_UART_NUM);
  }
  if (wake != ESP_OK) {
    ESP_LOGW(TAG, "UART light-sleep wakeup unavailable: %s", esp_err_to_name(wake));
  }
#endif

  ESP_LOGI(TAG, "Console started. Type 'help' for commands.");
  return ESP_OK;
}

void console_service_stop(void) {
  if (s_repl != NULL && s_repl->del != NULL) {
    s_repl->del(s_repl);
    s_repl = NULL;
  }
}
