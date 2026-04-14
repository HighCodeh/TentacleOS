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

#include "ys_rfid2_hal_uart.h"

#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RFID_HAL_UART";

#define RFID_UART_BUF_SIZE 256

static int s_port = -1;
static bool s_is_init = false;

esp_err_t ys_rfid2_hal_uart_init(const ys_rfid2_hal_uart_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_is_init) {
    return ESP_ERR_INVALID_STATE;
  }

  uart_config_t uart_config = {
      .baud_rate = config->baud_rate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t ret = uart_driver_install(config->port, RFID_UART_BUF_SIZE, 0, 0, NULL, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = uart_param_config(config->port, &uart_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
    uart_driver_delete(config->port);
    return ret;
  }

  ret = uart_set_pin(config->port, config->tx_pin, config->rx_pin, -1, -1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
    uart_driver_delete(config->port);
    return ret;
  }

  s_port = config->port;
  s_is_init = true;

  ESP_LOGI(TAG, "Initialized — port: %d, baud: %d", config->port, config->baud_rate);
  return ESP_OK;
}

void ys_rfid2_hal_uart_deinit(void) {
  if (!s_is_init) {
    return;
  }

  uart_driver_delete(s_port);
  s_port = -1;
  s_is_init = false;

  ESP_LOGI(TAG, "Deinitialized");
}

int ys_rfid2_hal_uart_read(uint8_t *out_data, size_t len, uint32_t timeout_ms) {
  if (!s_is_init || out_data == NULL) {
    return -1;
  }

  return uart_read_bytes(s_port, out_data, len, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t ys_rfid2_hal_uart_write(const uint8_t *data, size_t len) {
  if (!s_is_init || data == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  int written = uart_write_bytes(s_port, data, len);
  if (written < 0) {
    return ESP_FAIL;
  }

  return ESP_OK;
}

void ys_rfid2_hal_uart_flush(void) {
  if (!s_is_init) {
    return;
  }

  uart_flush_input(s_port);
}
