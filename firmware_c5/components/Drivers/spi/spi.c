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

#include "spi.h"

#include <string.h>

#include "esp_log.h"

#include "pin_def.h"

static const char *TAG = "SPI_MASTER_C5";

#define SPI_MAX_TRANSFER_SIZE 4096

static spi_device_handle_t s_device_handles[SPI_DEVICE_MAX] = {NULL};
static bool s_is_initialized = false;

esp_err_t spi_init(void) {
  if (s_is_initialized) {
    return ESP_OK;
  }

  spi_bus_config_t buscfg = {
      .mosi_io_num = GPIO_SPI_MOSI_PIN,
      .sclk_io_num = GPIO_SPI_SCLK_PIN,
      .miso_io_num = GPIO_SPI_MISO_PIN,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = SPI_MAX_TRANSFER_SIZE,
  };

  esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret == ESP_OK) {
    s_is_initialized = true;
    ESP_LOGI(TAG, "SPI bus initialized");
  }
  return ret;
}

esp_err_t spi_add_device(spi_device_id_t id, const spi_device_config_t *config) {
  if (id >= SPI_DEVICE_MAX || !s_is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_device_handles[id] != NULL) {
    return ESP_OK;
  }

  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = config->clock_speed_hz,
      .mode = config->mode,
      .spics_io_num = config->cs_pin,
      .queue_size = config->queue_size,
  };

  return spi_bus_add_device(SPI2_HOST, &devcfg, &s_device_handles[id]);
}

spi_device_handle_t spi_get_handle(spi_device_id_t id) {
  return (id < SPI_DEVICE_MAX) ? s_device_handles[id] : NULL;
}

esp_err_t spi_transmit(spi_device_id_t id, const uint8_t *data, size_t len) {
  if (id >= SPI_DEVICE_MAX || s_device_handles[id] == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  spi_transaction_t trans = {
      .length = len * 8,
      .tx_buffer = data,
  };
  return spi_device_transmit(s_device_handles[id], &trans);
}

esp_err_t spi_deinit(void) {
  for (int i = 0; i < SPI_DEVICE_MAX; i++) {
    if (s_device_handles[i] != NULL) {
      spi_bus_remove_device(s_device_handles[i]);
      s_device_handles[i] = NULL;
    }
  }
  if (s_is_initialized) {
    spi_bus_free(SPI2_HOST);
    s_is_initialized = false;
  }
  return ESP_OK;
}
