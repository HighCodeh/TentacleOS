#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t spi_bus_remove_device(int handle);
esp_err_t spi_bus_get_max_transaction_len(int host, size_t *max_len);

#define ESP_SPI_DMA_BUF_ALIGN 4

#ifdef __cplusplus
}
#endif
