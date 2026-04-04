#include "spi_bridge_phy.h"
#include "spi.h"
#include "pin_def.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SPI_PHY_P4";
static SemaphoreHandle_t irq_semaphore = NULL;

static void IRAM_ATTR irq_handler(void* arg) {
    xSemaphoreGiveFromISR(irq_semaphore, NULL);
}

esp_err_t spi_bridge_phy_init(void) {
    irq_semaphore = xSemaphoreCreateBinary();

    // Init GPSPI2 for C5 Bridge
    esp_err_t ret = spi_bus_init(SPI2_HOST, BRIDGE_MOSI_PIN, BRIDGE_MISO_PIN, BRIDGE_SCLK_PIN);
    if (ret != ESP_OK) return ret;

    spi_device_config_t devcfg = {
        .cs_pin = BRIDGE_CS_PIN,
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .queue_size = 7
    };

    ret = spi_add_device(SPI2_HOST, SPI_DEVICE_BRIDGE, &devcfg);
    if (ret != ESP_OK) return ret;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .pin_bit_mask = (1ULL << BRIDGE_IRQ_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = 1
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BRIDGE_IRQ_PIN, irq_handler, NULL);

    return ESP_OK;
}

esp_err_t spi_bridge_phy_transmit(const uint8_t *tx_data, uint8_t *rx_data, size_t len) {
    spi_device_handle_t handle = spi_get_handle(SPI_DEVICE_BRIDGE);
    if (!handle) return ESP_ERR_INVALID_STATE;

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    return spi_device_polling_transmit(handle, &t);
}

esp_err_t spi_bridge_phy_wait_irq(uint32_t timeout_ms) {
    if (xSemaphoreTake(irq_semaphore, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}
