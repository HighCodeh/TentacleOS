#include "spi.h"
#include "pin_def.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SPI_MASTER_C5";

static spi_device_handle_t device_handles[SPI_DEVICE_MAX] = {NULL};
static bool bus_initialized = false;

esp_err_t spi_init(void) {
    if (bus_initialized) return ESP_OK;

    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_MOSI_PIN,
        .sclk_io_num = SPI_SCLK_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret == ESP_OK) {
        bus_initialized = true;
        ESP_LOGI(TAG, "Internal SPI Master initialized");
    }
    return ret;
}

esp_err_t spi_add_device(spi_device_id_t id, const spi_device_config_t *config) {
    if (id >= SPI_DEVICE_MAX || !bus_initialized) return ESP_ERR_INVALID_STATE;
    if (device_handles[id] != NULL) return ESP_OK;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = config->clock_speed_hz,
        .mode = config->mode,
        .spics_io_num = config->cs_pin,
        .queue_size = config->queue_size,
    };

    return spi_bus_add_device(SPI2_HOST, &devcfg, &device_handles[id]);
}

spi_device_handle_t spi_get_handle(spi_device_id_t id) {
    return (id < SPI_DEVICE_MAX) ? device_handles[id] : NULL;
}

esp_err_t spi_transmit(spi_device_id_t id, const uint8_t *data, size_t len) {
    if (id >= SPI_DEVICE_MAX || device_handles[id] == NULL) return ESP_ERR_INVALID_ARG;

    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
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
        spi_bus_free(SPI2_HOST);
        bus_initialized = false;
    }
    return ESP_OK;
}
