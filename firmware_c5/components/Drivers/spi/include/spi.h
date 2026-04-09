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

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/spi_master.h"

/**
 * @brief Registered SPI device identifiers.
 */
typedef enum {
  SPI_DEVICE_ST7789,
  SPI_DEVICE_CC1101,
  SPI_DEVICE_SD_CARD,
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
 * @brief Initialize the SPI bus using pin_def.h pins.
 *
 * @return ESP_OK on success or if already initialized.
 */
esp_err_t spi_init(void);

/**
 * @brief Add a device to the SPI bus.
 *
 * @param id      Device identifier.
 * @param config  Device configuration.
 * @return ESP_OK on success or if device already registered.
 */
esp_err_t spi_add_device(spi_device_id_t id, const spi_device_config_t *config);

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
 * @return ESP_OK on success, or an error code.
 */
esp_err_t spi_transmit(spi_device_id_t id, const uint8_t *data, size_t len);

/**
 * @brief Deinitialize the SPI bus and free all devices.
 *
 * @return ESP_OK on success.
 */
esp_err_t spi_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // SPI_H
