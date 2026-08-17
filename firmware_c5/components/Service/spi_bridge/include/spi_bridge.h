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

#ifndef SPI_BRIDGE_C5_H
#define SPI_BRIDGE_C5_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "spi_protocol.h"

/**
 * @brief Initialize the SPI slave and IRQ pin.
 *
 * @return
 *   - ESP_OK on success
 *   - Error code from the SPI slave driver on failure
 */
esp_err_t spi_bridge_slave_init(void);

/**
 * @brief Initialize the SPI slave in a specific handshake mode.
 *
 * spi_bridge_slave_init() is the same as this with SPI_BRIDGE_MODE_IRQ. In
 * SPI_BRIDGE_MODE_POLL the slave never pulses the IRQ line (the board has no
 * IRQ trace); the P4 master must be initialized in the matching mode.
 *
 * @param mode  SPI_BRIDGE_MODE_IRQ (default) or SPI_BRIDGE_MODE_POLL.
 * @return ESP_OK on success, or an error code from the SPI slave driver.
 */
esp_err_t spi_bridge_slave_init_mode(spi_bridge_mode_t mode);

/**
 * @brief Point the bridge to a fixed-size result set in memory.
 *
 * @param source    Pointer to the result array.
 * @param count     Number of items in the array.
 * @param item_size Size of each item in bytes.
 */
void spi_bridge_provide_results(void *source, uint16_t count, uint8_t item_size);

/**
 * @brief Point the bridge to a dynamically-sized result set in memory.
 *
 * @param source    Pointer to the result array.
 * @param count_ptr Pointer to the live item count.
 * @param item_size Size of each item in bytes.
 */
void spi_bridge_provide_results_dynamic(void *source, const uint16_t *count_ptr, uint8_t item_size);

/**
 * @brief Run a scan asynchronously off the SPI handler.
 *
 * @p fn does the blocking scan and calls spi_bridge_provide_results when done.
 * Returns false if a scan is already running (respond SPI_STATUS_BUSY). The P4
 * polls a *_SCAN_STATUS command (which returns spi_bridge_async_scan_busy) until
 * it clears, then fetches the results.
 */
bool spi_bridge_async_scan_start(void (*fn)(void));
bool spi_bridge_async_scan_busy(void);

/**
 * @brief Check whether streaming is enabled for a given SPI ID.
 *
 * @param id  SPI function identifier.
 * @return true if streaming is enabled, false otherwise.
 */
bool spi_bridge_stream_is_enabled(spi_id_t id);

/**
 * @brief Enable or disable streaming for a given SPI ID.
 *
 * @param id      SPI function identifier.
 * @param enable  true to enable, false to disable.
 */
void spi_bridge_stream_enable(spi_id_t id, bool enable);

/**
 * @brief Push a frame into the stream queue for the master to poll.
 *
 * @param id   SPI function identifier.
 * @param data Pointer to the frame data.
 * @param len  Length of the frame data in bytes.
 * @return true if the frame was enqueued, false if the queue is full or
 *         streaming is disabled.
 */
bool spi_bridge_stream_push(spi_id_t id, const uint8_t *data, uint8_t len);

/**
 * @brief Pulse the IRQ pin to notify the master (P4) that data is ready.
 */
void spi_bridge_notify_master(void);

/**
 * @brief Count of valid commands processed from the P4 since boot.
 *
 * A nonzero value means the P4 has reached the C5 over the bridge, which is the
 * OTA boot-health signal (see ota_post_boot_check).
 */
uint32_t spi_bridge_commands_processed(void);

#ifdef __cplusplus
}
#endif

#endif // SPI_BRIDGE_C5_H
