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

#ifndef SPI_H
#define SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "driver/spi_master.h"

/**
 * @brief Registered SPI device identifiers.
 */
typedef enum {
  SPI_DEVICE_ST7789 = 0,
  SPI_DEVICE_CC1101,
  SPI_DEVICE_SD_CARD,
  SPI_DEVICE_BRIDGE,
  SPI_DEVICE_MAX
} spi_device_id_t;

/**
 * @brief Configuration for adding a device to the SPI bus.
 */
typedef struct {
  int cs_pin;
  int clock_speed_hz;
  int mode;
  int queue_size;
} spi_device_config_t;

/**
 * @brief Initialize a raw SPI bus with explicit pin assignments.
 *
 * @param host  SPI host (SPI2_HOST, SPI3_HOST).
 * @param mosi  MOSI GPIO number.
 * @param miso  MISO GPIO number.
 * @param sclk  SCLK GPIO number.
 * @return
 *   - ESP_OK on success or if already initialized
 *   - ESP_ERR_INVALID_ARG if host is out of range
 */
esp_err_t spi_bus_init(spi_host_device_t host, int mosi, int miso, int sclk);

/**
 * @brief Initialize the main SPI bus (SPI3_HOST) using pin_def.h pins.
 *
 * @return ESP_OK on success.
 */
esp_err_t spi_init(void);

/**
 * @brief Add a device to an initialized SPI bus.
 *
 * @param host    SPI host the device belongs to.
 * @param id      Device identifier enum.
 * @param config  Device configuration (CS pin, clock, mode, queue size).
 * @return
 *   - ESP_OK on success or if device already registered
 *   - ESP_ERR_INVALID_STATE if bus is not initialized
 */
esp_err_t
spi_add_device(spi_host_device_t host, spi_device_id_t id, const spi_device_config_t *config);

/**
 * @brief Get the ESP-IDF handle for a registered device.
 *
 * @param id  Device identifier.
 * @return Handle, or NULL if not registered.
 */
spi_device_handle_t spi_get_handle(spi_device_id_t id);

/**
 * @brief Perform a blocking SPI transmission.
 *
 * @param id    Device identifier.
 * @param data  Pointer to TX data buffer.
 * @param len   Number of bytes to transmit.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if device is not registered
 */
esp_err_t spi_transmit(spi_device_id_t id, const uint8_t *data, size_t len);

/**
 * @brief Deinitialize an SPI bus and free resources.
 *
 * @param host  SPI host to deinitialize.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if bus is not active
 */
esp_err_t spi_bus_deinit(spi_host_device_t host);

#ifdef __cplusplus
}
#endif

#endif // SPI_H
