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

#include "driver/spi_slave.h"
#include "esp_err.h"

/**
 * @brief Initialize the SPI slave driver for P4-to-C5 bridge communication.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t spi_slave_driver_init(void);

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
 * @brief Set the IRQ output level to signal the master.
 *
 * @param level  GPIO level (0 or 1).
 */
void spi_slave_driver_set_irq(int level);

#ifdef __cplusplus
}
#endif

#endif // SPI_SLAVE_DRIVER_H
