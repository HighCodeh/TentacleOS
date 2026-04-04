#include "spi_slave_driver.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "pin_def.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SPI_SLAVE_DRV";

esp_err_t spi_slave_driver_init(void) {
    spi_bus_config_t buscfg = {
        .miso_io_num = BRIDGE_MISO_PIN,
        .mosi_io_num = BRIDGE_MOSI_PIN,
        .sclk_io_num = BRIDGE_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = BRIDGE_CS_PIN,
        .queue_size = 3,
        .flags = 0,
    };

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BRIDGE_IRQ_PIN),
        .pull_down_en = 1,
        .pull_up_en = 0
    };
    gpio_config(&io_conf);
    gpio_set_level(BRIDGE_IRQ_PIN, 0);

    esp_err_t ret = spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI Slave: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPI Slave Driver initialized (Link P4)");
    return ESP_OK;
}

esp_err_t spi_slave_driver_transmit(const uint8_t *tx_data, uint8_t *rx_data, size_t len) {
    spi_slave_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    return spi_slave_transmit(SPI2_HOST, &t, portMAX_DELAY);
}

void spi_slave_driver_set_irq(int level) {
    gpio_set_level(BRIDGE_IRQ_PIN, level);
}