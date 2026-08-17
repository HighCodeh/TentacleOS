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

#ifndef SPI_BRIDGE_PHY_H
#define SPI_BRIDGE_PHY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>

#include "esp_err.h"
#include "driver/spi_master.h"

#define BRIDGE_SPI_HOST SPI2_HOST

/**
 * @brief Initialize the SPI bridge physical layer with the IRQ line (default).
 *
 * Configures SPI2 as master for P4-to-C5 communication and sets up
 * the IRQ GPIO interrupt.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t spi_bridge_phy_init(void);

/**
 * @brief Initialize the SPI bridge physical layer, optionally without the IRQ.
 *
 * When setup_irq is false (POLL mode) the IRQ GPIO/ISR is not configured,
 * because the board has no IRQ trace. The master then polls the bus instead of
 * waiting on spi_bridge_phy_wait_irq().
 *
 * @param setup_irq  true to wire the IRQ interrupt, false for polled mode.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t spi_bridge_phy_init_ex(bool setup_irq);

/**
 * @brief Perform a full-duplex SPI transaction on the bridge bus.
 *
 * @param tx_data  Transmit buffer (may be NULL for receive-only).
 * @param rx_data  Receive buffer (may be NULL for transmit-only).
 * @param len      Number of bytes to transfer.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t spi_bridge_phy_transmit(const uint8_t *tx_data, uint8_t *rx_data, size_t len);

/**
 * @brief Wait for an IRQ pulse from the C5 slave.
 *
 * @param timeout_ms  Maximum wait time in milliseconds.
 * @return ESP_OK if IRQ received, ESP_ERR_TIMEOUT otherwise.
 */
esp_err_t spi_bridge_phy_wait_irq(uint32_t timeout_ms);

/**
 * @brief Enter a legacy (InkTest-compatible) SPI session.
 *
 * Removes the normal 10 MHz TentacleOS bridge device and adds a 4 MHz mode-0
 * device matching the InkTest master, so the P4 can talk to a C5 still running
 * InkTest (e.g. to command it into ROM download mode for reflashing). The
 * normal bridge must be quiesced first via spi_bridge_suspend(true).
 *
 * @return ESP_OK on success, or an error code (the normal device is restored
 *         on failure).
 */
esp_err_t spi_bridge_phy_legacy_begin(void);

/**
 * @brief Full-duplex transaction on the legacy (InkTest) device.
 *
 * @param tx_data  Transmit buffer (DMA-capable, word-aligned).
 * @param rx_data  Receive buffer (DMA-capable, word-aligned), may be NULL.
 * @param len      Number of bytes to transfer.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if no legacy session.
 */
esp_err_t spi_bridge_phy_legacy_transmit(const uint8_t *tx_data, uint8_t *rx_data, size_t len);

/**
 * @brief End a legacy SPI session and restore the normal bridge device.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t spi_bridge_phy_legacy_end(void);

#ifdef __cplusplus
}
#endif

#endif // SPI_BRIDGE_PHY_H
