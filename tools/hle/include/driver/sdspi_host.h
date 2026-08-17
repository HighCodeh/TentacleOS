#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/spi_master.h"

esp_err_t sdspi_host_init(void);
esp_err_t sdspi_host_init_device(const void *cfg, void **handle);
esp_err_t sdspi_host_remove_device(void *handle);
esp_err_t sdspi_host_deinit(void);

#ifdef __cplusplus
}
#endif
