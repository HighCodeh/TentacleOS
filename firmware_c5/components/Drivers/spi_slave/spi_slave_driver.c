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

#include "spi_slave_driver.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "pin_def.h"

static const char *TAG = "SPI_SLAVE_DRV";

esp_err_t spi_slave_driver_init(void) {
  spi_bus_config_t buscfg = {
      .miso_io_num = GPIO_BRIDGE_MISO_PIN,
      .mosi_io_num = GPIO_BRIDGE_MOSI_PIN,
      .sclk_io_num = GPIO_BRIDGE_SCLK_PIN,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };

  spi_slave_interface_config_t slvcfg = {
      .mode = 0,
      .spics_io_num = GPIO_BRIDGE_CS_PIN,
      .queue_size = 3,
      .flags = 0,
  };

  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << GPIO_BRIDGE_IRQ_PIN),
      .pull_down_en = 1,
      .pull_up_en = 0,
  };
  gpio_config(&io_conf);
  gpio_set_level(GPIO_BRIDGE_IRQ_PIN, 0);

  esp_err_t ret = spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI slave: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "SPI slave driver initialized");
  return ESP_OK;
}

esp_err_t spi_slave_driver_transmit(const uint8_t *tx_data, uint8_t *rx_data, size_t len) {
  spi_slave_transaction_t t = {
      .length = len * 8,
      .tx_buffer = tx_data,
      .rx_buffer = rx_data,
  };
  return spi_slave_transmit(SPI2_HOST, &t, portMAX_DELAY);
}

esp_err_t spi_slave_driver_queue(spi_slave_transaction_t *trans,
                                 const uint8_t *tx_data,
                                 uint8_t *rx_data,
                                 size_t len) {
  memset(trans, 0, sizeof(*trans));
  trans->length = len * 8;
  trans->tx_buffer = tx_data;
  trans->rx_buffer = rx_data;
  return spi_slave_queue_trans(SPI2_HOST, trans, portMAX_DELAY);
}

esp_err_t spi_slave_driver_wait(void) {
  spi_slave_transaction_t *result = NULL;
  return spi_slave_get_trans_result(SPI2_HOST, &result, portMAX_DELAY);
}

void spi_slave_driver_set_irq(int level) {
  gpio_set_level(GPIO_BRIDGE_IRQ_PIN, level);
}
