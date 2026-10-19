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

#include "rnode_serial.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "RNODE_SERIAL";

#define RNODE_UART_PORT   UART_NUM_0
#define RNODE_UART_TX_PIN 43
#define RNODE_UART_RX_PIN 44
#define RNODE_UART_BAUD   115200
#define RNODE_UART_RX_BUF 2048
#define RNODE_UART_TX_BUF 2048

bool rnode_serial_is_supported(void) {
#ifdef CONFIG_IDF_TARGET_ESP32P4
  return false;
#else
  return true;
#endif
}

esp_err_t rnode_serial_init(void) {
  if (!rnode_serial_is_supported()) {
    ESP_LOGE(TAG, "Legacy UART transport conflicts with P4 SDMMC pins");
    return ESP_ERR_NOT_SUPPORTED;
  }
  uart_config_t cfg = {
      .baud_rate = RNODE_UART_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t ret =
      uart_driver_install(RNODE_UART_PORT, RNODE_UART_RX_BUF, RNODE_UART_TX_BUF, 0, NULL, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = uart_param_config(RNODE_UART_PORT, &cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = uart_set_pin(RNODE_UART_PORT,
                     RNODE_UART_TX_PIN,
                     RNODE_UART_RX_PIN,
                     UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG,
           "UART%d @ %d baud (TX=GPIO%d RX=GPIO%d)",
           RNODE_UART_PORT,
           RNODE_UART_BAUD,
           RNODE_UART_TX_PIN,
           RNODE_UART_RX_PIN);
  return ESP_OK;
}

int rnode_serial_write(const uint8_t *data, size_t len) {
  if (!rnode_serial_is_supported()) {
    return -1;
  }
  if (data == NULL || len == 0) {
    return 0;
  }
  int n = uart_write_bytes(RNODE_UART_PORT, (const char *)data, len);
  return n;
}

int rnode_serial_read(uint8_t *out, size_t cap) {
  if (!rnode_serial_is_supported()) {
    return -1;
  }
  if (out == NULL || cap == 0) {
    return 0;
  }
  int n = uart_read_bytes(RNODE_UART_PORT, out, cap, 0);
  if (n < 0) {
    return 0;
  }
  return n;
}
