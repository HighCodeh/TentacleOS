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

#ifndef YS_RFID2_HAL_UART_H
#define YS_RFID2_HAL_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief HAL UART configuration.
 */
typedef struct {
  int port;
  int baud_rate;
  int tx_pin;
  int rx_pin;
} ys_rfid2_hal_uart_config_t;

/**
 * @brief Initialize the UART peripheral.
 *
 * @param config  Pointer to configuration. Caller retains ownership.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if config is NULL
 *   - ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t ys_rfid2_hal_uart_init(const ys_rfid2_hal_uart_config_t *config);

/**
 * @brief Deinitialize the UART peripheral and release resources.
 */
void ys_rfid2_hal_uart_deinit(void);

/**
 * @brief Read bytes from UART.
 *
 * @param[out] out_data    Buffer to store received bytes.
 * @param      len         Maximum number of bytes to read.
 * @param      timeout_ms  Read timeout in milliseconds.
 * @return Number of bytes actually read, or -1 on error.
 */
int ys_rfid2_hal_uart_read(uint8_t *out_data, size_t len, uint32_t timeout_ms);

/**
 * @brief Write bytes to UART.
 *
 * @param data  Buffer containing bytes to send.
 * @param len   Number of bytes to send.
 * @return
 *   - ESP_OK on success
 *   - ESP_FAIL on write error
 */
esp_err_t ys_rfid2_hal_uart_write(const uint8_t *data, size_t len);

/**
 * @brief Flush the UART input buffer.
 */
void ys_rfid2_hal_uart_flush(void);

#ifdef __cplusplus
}
#endif

#endif // YS_RFID2_HAL_UART_H
