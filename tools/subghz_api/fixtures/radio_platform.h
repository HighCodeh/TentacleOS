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

#ifndef RADIO_TEST_PLATFORM_H
#define RADIO_TEST_PLATFORM_H

#include "test_platform.h"

#define ESP_RETURN_ON_ERROR(expr, tag, ...) do { \
  esp_err_t test_error = (expr); \
  if (test_error != ESP_OK) return test_error; \
} while (0)
#define GPIO_RF_SW_V1_PIN 17
#define GPIO_RF_SW_V2_PIN 18
#define GPIO_CC1101_CS_PIN 7
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_INTR_DISABLE 0
#define SPI3_HOST 3
#define SPI_DEVICE_CC1101 1
#define SPI_TRANS_USE_TXDATA 1
#define SPI_TRANS_USE_RXDATA 2

typedef void *spi_device_handle_t;
typedef struct {
  unsigned length, flags;
  uint8_t tx_data[4], rx_data[4];
  const void *tx_buffer;
  void *rx_buffer;
} spi_transaction_t;
typedef struct { int clock_speed_hz, mode, cs_pin, queue_size; } spi_device_config_t;
typedef struct {
  uint64_t pin_bit_mask;
  int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;

esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(int pin, uint32_t level);
esp_err_t spi_add_device(int host, int id, const spi_device_config_t *config);
spi_device_handle_t spi_get_handle(int id);
bool spi_bus_lock_take(uint32_t timeout);
void spi_bus_lock_give(void);
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *transaction);
esp_err_t spi_device_transmit(spi_device_handle_t handle, spi_transaction_t *transaction);

#endif
