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

#include "driver/spi_master.h"
#include "esp_err.h"

typedef enum {
    SPI_DEVICE_ST7789,
    SPI_DEVICE_CC1101,
    SPI_DEVICE_SD_CARD,
    SPI_DEVICE_MAX
} spi_device_id_t;

typedef struct {
    int cs_pin;
    int clock_speed_hz;
    int mode;
    int queue_size;
} spi_device_config_t;

// Declarações das funções
esp_err_t spi_init(void);
esp_err_t spi_add_device(spi_device_id_t id, const spi_device_config_t *config);
spi_device_handle_t spi_get_handle(spi_device_id_t id);
esp_err_t spi_transmit(spi_device_id_t id, const uint8_t *data, size_t len);
esp_err_t spi_deinit(void);

#endif
