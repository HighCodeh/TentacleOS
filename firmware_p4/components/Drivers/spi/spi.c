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
#include "pin_def.h"
#include "esp_log.h"
#include <string.h>

#define TAG "SPI"

// Armazena handles de todos os devices
static spi_device_handle_t device_handles[SPI_DEVICE_MAX] = {NULL};
static bool bus_initialized = false;

esp_err_t spi_init(void) {
    if (bus_initialized) {
        ESP_LOGW(TAG, "SPI já inicializado");
        return ESP_OK;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_MOSI_PIN,
        .sclk_io_num = SPI_SCLK_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar barramento: %s", esp_err_to_name(ret));
        return ret;
    }

    bus_initialized = true;
    ESP_LOGI(TAG, "Barramento SPI inicializado");
    return ESP_OK;
}

esp_err_t spi_add_device(spi_device_id_t id, const spi_device_config_t *config) {
    if (id >= SPI_DEVICE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!bus_initialized) {
        ESP_LOGE(TAG, "Barramento não inicializado");
        return ESP_ERR_INVALID_STATE;
    }

    if (device_handles[id] != NULL) {
        ESP_LOGW(TAG, "Device %d já adicionado", id);
        return ESP_OK;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = config->clock_speed_hz,
        .mode = config->mode,
        .spics_io_num = config->cs_pin,
        .queue_size = config->queue_size,
        .flags = 0,
        .pre_cb = NULL,
    };

    esp_err_t ret = spi_bus_add_device(SPI3_HOST, &devcfg, &device_handles[id]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao adicionar device %d: %s", id, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Device %d adicionado com sucesso", id);
    return ESP_OK;
}

spi_device_handle_t spi_get_handle(spi_device_id_t id) {
    if (id >= SPI_DEVICE_MAX) {
        return NULL;
    }
    return device_handles[id];
}

esp_err_t spi_transmit(spi_device_id_t id, const uint8_t *data, size_t len) {
    if (id >= SPI_DEVICE_MAX || device_handles[id] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t trans = {
        .length = len * 8,  // em bits
        .tx_buffer = data,
        .rx_buffer = NULL,
    };

    return spi_device_transmit(device_handles[id], &trans);
}

esp_err_t spi_deinit(void) {
    for (int i = 0; i < SPI_DEVICE_MAX; i++) {
        if (device_handles[i] != NULL) {
            spi_bus_remove_device(device_handles[i]);
            device_handles[i] = NULL;
        }
    }

    if (bus_initialized) {
        spi_bus_free(SPI3_HOST);
        bus_initialized = false;
    }

    ESP_LOGI(TAG, "SPI desinicializado");
    return ESP_OK;
}
