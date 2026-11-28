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

#ifndef HEADER_SPI_H
#define HEADER_SPI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HEADER_SPI_TRANSFER_MAX 64U
#define HEADER_SPI_DEBUG_DM_PIN 7U
#define HEADER_SPI_DEBUG_DP_PIN 9U
typedef struct {
  uint32_t clock_hz;
  uint8_t cs_pin;
  uint8_t control_pin;
  uint8_t mode;
} header_spi_config_t;
/* Serialized firmware worker calls only; pin numbers follow docs/pins. */
esp_err_t header_spi_open(const header_spi_config_t *config);
esp_err_t header_spi_transfer(const uint8_t *tx, uint8_t *out_rx, size_t length);
esp_err_t header_spi_write(uint8_t pin, bool level);
esp_err_t header_spi_close(void);
#ifdef __cplusplus
}
#endif
#endif
