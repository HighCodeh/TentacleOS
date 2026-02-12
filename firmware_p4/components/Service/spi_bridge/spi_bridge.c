#include "spi_bridge.h"
#include "spi_bridge_phy.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SPI_BRIDGE_P4";

esp_err_t spi_bridge_master_init(void) {
    return spi_bridge_phy_init();
}

esp_err_t spi_bridge_send_command(spi_id_t id, const uint8_t *payload, uint8_t len, 
                                 spi_header_t *out_header, uint8_t *out_payload, 
                                 uint32_t timeout_ms) {
    spi_header_t header = {
        .sync = SPI_SYNC_BYTE,
        .type = SPI_TYPE_CMD,
        .id = id,
        .length = len
    };

    uint8_t tx_buf[260];
    memcpy(tx_buf, &header, sizeof(header));
    if (payload && len > 0) {
        memcpy(tx_buf + sizeof(header), payload, len);
    }

    esp_err_t ret = spi_bridge_phy_transmit(tx_buf, NULL, sizeof(header) + len);
    if (ret != ESP_OK) return ret;

    ret = spi_bridge_phy_wait_irq(timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Command 0x%02X timeout", id);
        return ret;
    }

    ret = spi_bridge_phy_transmit(NULL, (uint8_t*)out_header, sizeof(spi_header_t));
    if (ret != ESP_OK) return ret;

    if (out_header->sync != SPI_SYNC_BYTE) {
        ESP_LOGE(TAG, "Sync error: 0x%02X", out_header->sync);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (out_header->length > 0 && out_payload) {
        ret = spi_bridge_phy_transmit(NULL, out_payload, out_header->length);
    }

    return ret;
}