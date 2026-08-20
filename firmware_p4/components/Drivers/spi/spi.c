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

#include "spi.h"

#include <string.h>

#include "esp_log.h"

#include "pin_def.h"

static const char *TAG = "SPI_BUS";

#define SPI_MAX_TRANSFER_SIZE 49152

static spi_device_handle_t s_device_handles[SPI_DEVICE_MAX] = {NULL};
static bool s_bus_active[SOC_SPI_PERIPH_NUM] = {false};

static SemaphoreHandle_t s_spi3_lock = NULL;

esp_err_t spi_bus_init(spi_host_device_t host, int mosi, int miso, int sclk) {
  if (host >= SOC_SPI_PERIPH_NUM) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_bus_active[host]) {
    return ESP_OK;
  }

  spi_bus_config_t buscfg = {
      .mosi_io_num = mosi,
      .miso_io_num = miso,
      .sclk_io_num = sclk,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = SPI_MAX_TRANSFER_SIZE,
  };

  esp_err_t ret = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
  if (ret == ESP_OK) {
    s_bus_active[host] = true;
    ESP_LOGI(TAG, "Bus host %d initialized", host);
    if (host == SPI3_HOST && s_spi3_lock == NULL) {
      s_spi3_lock = xSemaphoreCreateBinary();
      if (s_spi3_lock != NULL) {
        xSemaphoreGive(s_spi3_lock);
      } else {
        ESP_LOGE(TAG, "Failed to create SPI3 bus lock");
      }
    }
  }

  return ret;
}

bool spi_bus_lock_take(uint32_t timeout_ms) {
  if (s_spi3_lock == NULL) {
    return true;
  }
  return xSemaphoreTake(s_spi3_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void spi_bus_lock_give(void) {
  if (s_spi3_lock != NULL) {
    xSemaphoreGive(s_spi3_lock);
  }
}

void spi_bus_lock_give_from_isr(BaseType_t *hpw) {
  if (s_spi3_lock != NULL) {
    xSemaphoreGiveFromISR(s_spi3_lock, hpw);
  }
}

esp_err_t spi_init(void) {
  return spi_bus_init(SPI3_HOST, GPIO_SPI_MOSI_PIN, GPIO_SPI_MISO_PIN, GPIO_SPI_SCLK_PIN);
}

esp_err_t
spi_add_device(spi_host_device_t host, spi_device_id_t id, const spi_device_config_t *config) {
  if (id >= SPI_DEVICE_MAX || !s_bus_active[host]) {
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

  return spi_bus_add_device(host, &devcfg, &s_device_handles[id]);
}

spi_device_handle_t spi_get_handle(spi_device_id_t id) {
  if (id >= SPI_DEVICE_MAX) {
    return NULL;
  }
  return s_device_handles[id];
}

esp_err_t spi_remove_device(spi_device_id_t id) {
  if (id >= SPI_DEVICE_MAX) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_device_handles[id] == NULL) {
    return ESP_OK;
  }
  esp_err_t ret = spi_bus_remove_device(s_device_handles[id]);
  if (ret == ESP_OK) {
    s_device_handles[id] = NULL;
  }
  return ret;
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

esp_err_t spi_bus_deinit(spi_host_device_t host) {
  if (host >= SOC_SPI_PERIPH_NUM || !s_bus_active[host]) {
    return ESP_ERR_INVALID_STATE;
  }

  spi_bus_free(host);
  s_bus_active[host] = false;

  return ESP_OK;
}
