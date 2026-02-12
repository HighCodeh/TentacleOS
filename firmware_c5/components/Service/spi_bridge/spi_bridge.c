#include "spi_bridge.h"
#include "spi_slave_driver.h"
#include "wifi_dispatcher.h"
#include "bt_dispatcher.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SPI_BRIDGE_C5";

void spi_bridge_notify_master(void) {
    spi_slave_driver_set_irq(1);
    vTaskDelay(pdMS_TO_TICKS(1)); 
    spi_slave_driver_set_irq(0);
}

static void spi_bridge_task(void *pvParameters) {
    uint8_t rx_buf[260];
    uint8_t tx_buf[260];
    uint8_t resp_payload[256];
    uint8_t resp_len = 0;

    while (1) {
        memset(rx_buf, 0, sizeof(rx_buf));
        
        esp_err_t ret = spi_slave_driver_transmit(NULL, rx_buf, sizeof(rx_buf));
        if (ret == ESP_OK) {
            spi_header_t *header = (spi_header_t *)rx_buf;
            if (header->sync == SPI_SYNC_BYTE && header->type == SPI_TYPE_CMD) {
                ESP_LOGI(TAG, "Received CMD: 0x%02X, Len: %d", header->id, header->length);
                
                spi_status_t status = SPI_STATUS_ERROR;

                if (header->id >= 0x10 && header->id <= 0x4F) {
                    status = wifi_dispatcher_execute(header->id, rx_buf + sizeof(spi_header_t), 
                                                   header->length, resp_payload, &resp_len);
                } else if (header->id >= 0x50 && header->id <= 0x7F) {
                    status = bt_dispatcher_execute(header->id, rx_buf + sizeof(spi_header_t), 
                                                  header->length, resp_payload, &resp_len);
                } else if (header->id == SPI_ID_SYSTEM_PING) {
                    status = SPI_STATUS_OK;
                    resp_len = 0;
                }

                spi_header_t resp_header = {
                    .sync = SPI_SYNC_BYTE,
                    .type = SPI_TYPE_RESP,
                    .id = header->id,
                    .length = resp_len + 1 
                };

                memset(tx_buf, 0, sizeof(tx_buf));
                memcpy(tx_buf, &resp_header, sizeof(resp_header));
                tx_buf[sizeof(resp_header)] = (uint8_t)status;
                if (resp_len > 0) {
                    memcpy(tx_buf + sizeof(resp_header) + 1, resp_payload, resp_len);
                }
                
                spi_bridge_notify_master();
                spi_slave_driver_transmit(tx_buf, NULL, sizeof(resp_header) + resp_header.length);
            }
        }
    }
}

esp_err_t spi_bridge_slave_init(void) {
    esp_err_t ret = spi_slave_driver_init();
    if (ret != ESP_OK) return ret;

    xTaskCreate(spi_bridge_task, "spi_bridge_task", 4096, NULL, 10, NULL);
    return ESP_OK;
}