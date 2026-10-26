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

#ifndef SPI_SLAVE_DRIVER_H
#define SPI_SLAVE_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "driver/spi_slave.h"
#include "esp_err.h"

/**
 * @brief Initialize the SPI slave driver for P4-to-C5 bridge communication.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t spi_slave_driver_init(void);

/**
 * Cumulative, saturating setup observations, cleared after successful init.
 * The bridge uses
 * TX-only response and RX-only request descriptors; a full
 * duplex descriptor, if used by another
 * caller, contributes to both groups.
 *
 * IDF calls post_setup_cb after user_start, so
 * *_cs_low_after_setup is only
 * an upper-bound candidate-overlap count: CS may have fallen
 * legitimately
 * after hardware start but before the sample. It does not prove an arming
 * race,
 * displacement, corruption or successful delivery. Zero does not rule
 * out overlap elsewhere. No
 * frame contents are recorded.
 */
typedef struct {
  uint32_t tx_armed;
  uint32_t tx_cs_low_after_setup;
  uint32_t rx_armed;
  uint32_t rx_cs_low_after_setup;
} spi_slave_setup_diag_t;

/** Get a coherent task-context snapshot without clearing the counters. */
esp_err_t spi_slave_driver_get_setup_diag(spi_slave_setup_diag_t *out);

/**
 * @brief Wait for and execute an SPI slave transaction.
 *
 * Blocks until the master initiates a transfer, then exchanges data.
 *
 * @param tx_data  Transmit buffer (may be NULL).
 * @param rx_data  Receive buffer (may be NULL).
 * @param len      Number of bytes to transfer.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t spi_slave_driver_transmit(const uint8_t *tx_data, uint8_t *rx_data, size_t len);

/**
 * @brief Queue an SPI slave transaction without blocking.
 *
 * Lets the caller keep a transaction armed in hardware at all times so a
 * transfer from the master is never missed. The @p trans descriptor must stay
 * valid until reaped by spi_slave_driver_wait().
 *
 * @param trans    Caller-owned transaction descriptor (filled in by this call).
 * @param tx_data  Transmit buffer (may be NULL).
 * @param rx_data  Receive buffer (may be NULL).
 * @param len      Number of bytes to transfer.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t spi_slave_driver_queue(spi_slave_transaction_t *trans,
                                 const uint8_t *tx_data,
                                 uint8_t *rx_data,
                                 size_t len);

/**
 * @brief Wait for the next queued SPI slave transaction to complete.
 *
 * Transactions complete in the order they were queued (FIFO).
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t spi_slave_driver_wait(void);

/**
 * Queue a response and its following receive as one bounded publication step.
 *
 * C5-only:
 * both DMA buffers must be word aligned, nonempty, distinct and at
 * most 2080 bytes, with
 * lengths
 * divisible by four. Descriptors are prepared
 * before the critical section; both queue
 * calls use zero wait. No transaction
 * may remain queued/in flight on entry. The caller is the
 * sole queue producer.
 *
 * On error retain descriptors/buffers until spi_slave_driver_recover
 * succeeds.
 * It drains tracked completions without resetting the queue or replaying commands.
 *
 * These APIs have one task owner; recovery must run on that same task.
 */
esp_err_t spi_slave_driver_queue_response_and_receive(spi_slave_transaction_t *tx_trans,
                                                      const uint8_t *tx_data,
                                                      size_t tx_len,
                                                      spi_slave_transaction_t *rx_trans,
                                                      uint8_t *rx_data,
                                                      size_t rx_len);

/**
 * Reap the expected descriptor and require its complete configured bit length.
 * Identity
 * mismatch is ESP_ERR_INVALID_RESPONSE; a reported length mismatch is
 * ESP_ERR_INVALID_SIZE (the
 * hardware may clamp excess clocks to the configured
 * length). On identity mismatch retain all
 * outstanding descriptors and fail
 * closed.
 */
esp_err_t spi_slave_driver_wait_for(spi_slave_transaction_t *expected);

/** Bounded completion wait; timeout leaves ownership with the driver. */
esp_err_t spi_slave_driver_wait_for_ticks(spi_slave_transaction_t *expected, TickType_t ticks);

/** Drain owned descriptors within the deadline; never reset an active DMA queue. */
esp_err_t spi_slave_driver_recover(uint32_t timeout_ms);

/**
 * @brief Set the IRQ output level to signal the master.
 *
 * @param level  GPIO level (0 or 1).
 */
void spi_slave_driver_set_irq(int level);

#ifdef __cplusplus
}
#endif

#endif // SPI_SLAVE_DRIVER_H
