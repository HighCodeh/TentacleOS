#include "spi_bridge.h"
#include "spi_slave_driver.h"
#include "wifi_dispatcher.h"
#include "bt_dispatcher.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"
#include "signal_monitor.h"
#include "bluetooth_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SPI_BRIDGE_C5";

static void*    current_data_source = NULL;
static uint16_t current_item_count = 0;
static uint8_t  current_item_size = 0;

void spi_bridge_provide_results(void* source, uint16_t count, uint8_t item_size) {
    current_data_source = source;
    current_item_count = count;
    current_item_size = item_size;
}

void spi_bridge_notify_master(void) {
    spi_slave_driver_set_irq(1);
    vTaskDelay(pdMS_TO_TICKS(1)); 
    spi_slave_driver_set_irq(0);
}

static void spi_bridge_task(void *pvParameters) {
    uint8_t rx_buf[260];
    uint8_t tx_buf[260];

    while (1) {
        memset(rx_buf, 0, sizeof(rx_buf));
        if (spi_slave_driver_transmit(NULL, rx_buf, sizeof(rx_buf)) != ESP_OK) continue;

        spi_header_t *header = (spi_header_t *)rx_buf;
        if (header->sync != SPI_SYNC_BYTE || header->type != SPI_TYPE_CMD) continue;

        spi_status_t status = SPI_STATUS_OK;
        uint8_t resp_payload[256];
        uint8_t resp_len = 0;

        if (header->id == SPI_ID_SYSTEM_DATA) {
            uint16_t index;
            memcpy(&index, rx_buf + sizeof(spi_header_t), 2);

            if (index == 0xFFFF) { // Pull Count
                memcpy(resp_payload, &current_item_count, 2);
                resp_len = 2;
            } else if (index == 0xEEEE) { // Pull Sniffer Stats
                sniffer_stats_t stats = {
                    .packets = wifi_sniffer_get_packet_count(),
                    .deauths = wifi_sniffer_get_deauth_count(),
                    .buffer_usage = wifi_sniffer_get_buffer_usage(),
                    .signal_rssi = signal_monitor_get_rssi(),
                    .handshake_captured = wifi_sniffer_handshake_captured(),
                    .pmkid_captured = wifi_sniffer_pmkid_captured()
                };
                memcpy(resp_payload, &stats, sizeof(stats));
                resp_len = sizeof(stats);
            } else if (current_data_source && index < current_item_count) { // Pull Item
                memcpy(resp_payload, (uint8_t*)current_data_source + (index * current_item_size), current_item_size);
                resp_len = current_item_size;
            } else { status = SPI_STATUS_ERROR; }
        } else if (header->id >= 0x10 && header->id <= 0x4F) {
            status = wifi_dispatcher_execute(header->id, rx_buf + sizeof(spi_header_t), header->length, resp_payload, &resp_len);
        } else if (header->id >= 0x50 && header->id <= 0x7F) {
            status = bt_dispatcher_execute(header->id, rx_buf + sizeof(spi_header_t), header->length, resp_payload, &resp_len);
        }

        spi_header_t resp_header = {.sync = SPI_SYNC_BYTE, .type = SPI_TYPE_RESP, .id = header->id, .length = resp_len + 1};
        memset(tx_buf, 0, sizeof(tx_buf));
        memcpy(tx_buf, &resp_header, sizeof(resp_header));
        tx_buf[sizeof(resp_header)] = (uint8_t)status;
        if (resp_len > 0) memcpy(tx_buf + sizeof(resp_header) + 1, resp_payload, resp_len);
        
        spi_bridge_notify_master();
        spi_slave_driver_transmit(tx_buf, NULL, sizeof(resp_header) + resp_header.length);
    }
}

esp_err_t spi_bridge_slave_init(void) {
    esp_err_t ret = spi_slave_driver_init();
    if (ret != ESP_OK) return ret;
    xTaskCreate(spi_bridge_task, "spi_bridge_task", 4096, NULL, 10, NULL);
    return ESP_OK;
}
