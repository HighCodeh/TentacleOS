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

esp_err_t spi_init(void);
esp_err_t spi_add_device(spi_device_id_t id, const spi_device_config_t *config);
spi_device_handle_t spi_get_handle(spi_device_id_t id);
esp_err_t spi_transmit(spi_device_id_t id, const uint8_t *data, size_t len);
esp_err_t spi_deinit(void);

#endif