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

#ifndef SX1262_CMD_H
#define SX1262_CMD_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "sx1262_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wait for BUSY pin to go LOW with timeout.
 *
 * DS Section 8.3.1: BUSY must be LOW before any SPI transaction.
 *
 * @param hal         Pointer to HAL.
 * @param timeout_ms  Maximum wait time. 0 = use default (100ms).
 * @return ESP_OK if BUSY went LOW, ESP_ERR_TIMEOUT otherwise.
 */
esp_err_t sx1262_cmd_wait_busy(sx1262_hal_t *hal, uint32_t timeout_ms);

/**
 * @brief Write a command to the SX1262.
 *
 * DS Section 10.1: [opcode][param0][param1]...[paramN]
 * lock() → wait_busy → cs_low → spi_transfer → cs_high → unlock()
 *
 * @param hal     Pointer to HAL.
 * @param opcode  Command opcode from sx1262_regs.h.
 * @param params  Parameter bytes (may be NULL if len == 0).
 * @param len     Number of parameter bytes.
 * @return ESP_OK on success.
 */
esp_err_t sx1262_cmd_write(sx1262_hal_t *hal, uint8_t opcode, const uint8_t *params, size_t len);

/**
 * @brief Read response from the SX1262.
 *
 * DS Section 10.1: [opcode][NOP(status)][data0]...[dataN]
 * lock() → wait_busy → cs_low → spi_transfer → cs_high → unlock()
 *
 * @param hal         Pointer to HAL.
 * @param opcode      Command opcode.
 * @param out_result  Buffer for response bytes (excluding status).
 * @param len         Number of bytes to read.
 * @return ESP_OK on success.
 */
esp_err_t sx1262_cmd_read(sx1262_hal_t *hal, uint8_t opcode, uint8_t *out_result, size_t len);

/**
 * @brief Write to registers via direct access.
 *
 * DS Section 13.2.1: [0x0D][addr_msb][addr_lsb][data0]...[dataN]
 *
 * @param hal   Pointer to HAL.
 * @param addr  16-bit register address.
 * @param data  Data bytes to write. Must not be NULL.
 * @param len   Number of bytes. Must be > 0.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if data is NULL or len is 0.
 */
esp_err_t
sx1262_cmd_write_register(sx1262_hal_t *hal, uint16_t addr, const uint8_t *data, size_t len);

/**
 * @brief Read from registers via direct access.
 *
 * DS Section 13.2.2: [0x1D][addr_msb][addr_lsb][NOP][data0]...[dataN]
 *
 * @param hal       Pointer to HAL.
 * @param addr      16-bit register address.
 * @param out_data  Buffer for read data. Must not be NULL.
 * @param len       Number of bytes to read. Must be > 0.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_data is NULL or len is 0.
 */
esp_err_t sx1262_cmd_read_register(sx1262_hal_t *hal, uint16_t addr, uint8_t *out_data, size_t len);

/**
 * @brief Write to data buffer.
 *
 * DS Section 13.2.3: [0x0E][offset][data0]...[dataN]
 *
 * @param hal     Pointer to HAL.
 * @param offset  Buffer start offset.
 * @param data    Data bytes to write. Must not be NULL.
 * @param len     Number of bytes. Must be > 0.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if data is NULL or len is 0.
 */
esp_err_t
sx1262_cmd_write_buffer(sx1262_hal_t *hal, uint8_t offset, const uint8_t *data, size_t len);

/**
 * @brief Read from data buffer.
 *
 * DS Section 13.2.4: [0x1E][offset][NOP][data0]...[dataN]
 *
 * @param hal       Pointer to HAL.
 * @param offset    Buffer start offset.
 * @param out_data  Buffer for read data. Must not be NULL.
 * @param len       Number of bytes to read. Must be > 0.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_data is NULL or len is 0.
 */
esp_err_t sx1262_cmd_read_buffer(sx1262_hal_t *hal, uint8_t offset, uint8_t *out_data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // SX1262_CMD_H
