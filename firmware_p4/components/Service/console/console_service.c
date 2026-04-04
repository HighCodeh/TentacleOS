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

#include "console_service.h"
#include "esp_console.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "CONSOLE";

esp_err_t console_service_init(void) {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    
    repl_config.prompt = "highboy> ";
    repl_config.max_cmdline_length = 512;

    ESP_ERROR_CHECK(esp_console_register_help_command());

    register_system_commands();
    register_fs_commands();
    register_wifi_commands();

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    ESP_LOGI(TAG, "Initializing USB Serial/JTAG Console (Native S3)");
    esp_console_dev_usb_serial_jtag_config_t usbjtag_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    ESP_LOGI(TAG, "Initializing USB CDC Console (TinyUSB)");
    esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_USB_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl));

#else
    ESP_LOGI(TAG, "Initializing UART Console");
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    //uart_config.rx_buffer_size = 1024; // Some versions don't expose this directly in the struct macro or name differs
    //uart_config.tx_buffer_size = 1024;
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    
    ESP_LOGI(TAG, "Console started. Type 'help' for commands.");
    return ESP_OK;
}
