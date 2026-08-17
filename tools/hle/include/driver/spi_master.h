#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "soc/spi_periph.h"
#include "esp_heap_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int spi_host_device_t;
typedef void *spi_device_handle_t;

#define SPI2_HOST 1
#define SPI3_HOST 2
#define SPI_DMA_CH_AUTO 0

typedef struct {
    int command_bits;
    int address_bits;
    int dummy_bits;
    int mode;
    int clock_speed_hz;
    int spics_io_num;
    int queue_size;
    int flags;
    int cs_ena_pretrans;
    int cs_ena_posttrans;
} spi_device_interface_config_t;

typedef struct {
    int mosi_io_num;
    int miso_io_num;
    int sclk_io_num;
    int quadwp_io_num;
    int quadhd_io_num;
    int max_transfer_sz;
    int flags;
} spi_bus_config_t;

typedef struct {
    size_t length;
    const void *tx_buffer;
    void *rx_buffer;
    uint32_t flags;
    uint8_t tx_data[4];
    uint8_t rx_data[4];
} spi_transaction_t;

#define SPI_TRANS_USE_TXDATA (1 << 0)
#define SPI_TRANS_USE_RXDATA (1 << 1)

esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *bus, int dma);
esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *dev, spi_device_handle_t *handle);
esp_err_t spi_device_transmit(spi_device_handle_t handle, const void *trans);
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, const void *trans);
esp_err_t spi_bus_remove_device(spi_device_handle_t handle);

#ifdef __cplusplus
}
#endif
