#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// spi_slave_transaction_t matches the real ESP-IDF struct
typedef struct {
    size_t length;
    void *trans_len;
    const void *tx_buffer;
    void *rx_buffer;
} spi_slave_transaction_t;

typedef struct {
    int spics_io_num;
    int queue_size;
    int mode;
    int flags;
} spi_slave_interface_config_t;

esp_err_t spi_slave_initialize(int host, const spi_bus_config_t *bus, const spi_slave_interface_config_t *cfg, int dma);
esp_err_t spi_slave_transmit(int host, spi_slave_transaction_t *trans, int timeout);

#ifdef __cplusplus
}
#endif
