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

#ifndef ST25R3916_IRQ_H
#define ST25R3916_IRQ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Snapshot of all five IRQ status registers.
 */
typedef struct {
  uint8_t main;      /**< REG_MAIN_INT (0x1A) */
  uint8_t timer;     /**< REG_TIMER_NFC_INT (0x1B) */
  uint8_t error;     /**< REG_ERROR_INT (0x1C) */
  uint8_t target;    /**< REG_TARGET_INT (0x1D) */
  uint8_t collision; /**< REG_COLLISION (0x20) */
} st25r3916_irq_status_t;

/**
 * @brief Read and clear all five IRQ status registers.
 *
 * @param[out] out_status Pointer to store the result.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if pointer is NULL.
 */
esp_err_t st25r3916_irq_read(st25r3916_irq_status_t *out_status);

/**
 * @brief Clear latched interrupt status before starting a new RF exchange.
 *
 * The ST25R3916B
 * interrupt status registers are read-to-clear. In
 * particular, a delayed CRC/parity indication
 * from the previous receive
 * must not be mistaken for a failure of the next transmission.
 */
esp_err_t st25r3916_irq_clear(void);

/**
 * @brief Read IRQ status and log all fields at WARN level.
 *
 * @param ctx Context string.
 * @param fifo_count Current FIFO byte count.
 */
void st25r3916_irq_log(const char *ctx, uint16_t fifo_count);

/**
 * @brief Poll the IRQ pin until TXE is asserted or timeout expires.
 *
 * @return
 * - ESP_OK if TXE was received.
 * - ESP_ERR_TIMEOUT on timeout.
 * - ESP_FAIL on hardware error.
 */
esp_err_t st25r3916_irq_wait_txe(void);

/**
 * @brief Wait for the receiver to finish a complete RF frame.
 *
 * The interrupt registers are
 * read-to-clear, therefore this routine also
 * accumulates every status bit observed while
 * waiting.  Callers must wait for
 * RXE before draining the FIFO; a non-empty FIFO only means that
 * reception is
 * in progress, not that the frame is complete.
 *
 * @param timeout_ms Maximum wait
 * in milliseconds.
 * @param[out] out_status Accumulated interrupt snapshot. May be NULL.
 *
 * @return ESP_OK on RXE, ESP_ERR_TIMEOUT when no complete frame arrived, or
 *         ESP_FAIL on
 * an SPI read failure.
 */
esp_err_t st25r3916_irq_wait_rxe(int32_t timeout_ms, st25r3916_irq_status_t *out_status);

#ifdef __cplusplus
}
#endif

#endif // ST25R3916_IRQ_H
