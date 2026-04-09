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

#ifdef __cplusplus
}
#endif

#endif // SPI_BRIDGE_C5_H
