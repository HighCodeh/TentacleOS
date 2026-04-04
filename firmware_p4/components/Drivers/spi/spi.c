#include "spi.h"
#include "pin_def.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SPI_BUS_MGR";

static spi_device_handle_t device_handles[SPI_DEVICE_MAX] = {NULL};
static bool bus_active[SOC_SPI_PERIPH_NUM] = {false};

esp_err_t spi_bus_init(spi_host_device_t host, int mosi, int miso, int sclk) {
    if (host >= SOC_SPI_PERIPH_NUM) return ESP_ERR_INVALID_ARG;
    if (bus_active[host]) return ESP_OK;

    spi_bus_config_t buscfg = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32768,
    };

    esp_err_t ret = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret == ESP_OK) {
        bus_active[host] = true;
        ESP_LOGI(TAG, "Bus host %d initialized", host);
    }
    return ret;
}

esp_err_t spi_init(void) {
    return spi_bus_init(SPI3_HOST, SPI_MOSI_PIN, SPI_MISO_PIN, SPI_SCLK_PIN);
}

esp_err_t spi_add_device(spi_host_device_t host, spi_device_id_t id, const spi_device_config_t *config) {
    if (id >= SPI_DEVICE_MAX || !bus_active[host]) return ESP_ERR_INVALID_STATE;
    if (device_handles[id] != NULL) return ESP_OK;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = config->clock_speed_hz,
        .mode = config->mode,
        .spics_io_num = config->cs_pin,
        .queue_size = config->queue_size,
    };

    return spi_bus_add_device(host, &devcfg, &device_handles[id]);
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

esp_err_t spi_bus_deinit(spi_host_device_t host) {
    if (host >= SOC_SPI_PERIPH_NUM || !bus_active[host]) return ESP_ERR_INVALID_STATE;
    
    spi_bus_free(host);
    bus_active[host] = false;
    return ESP_OK;
}