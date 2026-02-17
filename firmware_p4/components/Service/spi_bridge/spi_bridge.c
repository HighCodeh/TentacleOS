#include "spi_bridge.h"
#include "spi_bridge_phy.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SPI_BRIDGE_P4";
static SemaphoreHandle_t spi_mutex = NULL;
static TaskHandle_t stream_task_handle = NULL;
static volatile bool command_in_flight = false;
static spi_stream_cb_t stream_cb_wifi = NULL;
static spi_stream_cb_t stream_cb_bt = NULL;

static void spi_bridge_stream_task(void *arg);
static esp_err_t spi_bridge_fetch_stream(spi_header_t *out_header, uint8_t *out_payload, uint8_t *out_len);

static esp_err_t spi_status_to_err(spi_status_t status) {
    switch (status) {
        case SPI_STATUS_OK: return ESP_OK;
        case SPI_STATUS_BUSY: return ESP_ERR_INVALID_STATE;
        case SPI_STATUS_UNSUPPORTED: return ESP_ERR_NOT_SUPPORTED;
        case SPI_STATUS_INVALID_ARG: return ESP_ERR_INVALID_ARG;
        case SPI_STATUS_ERROR:
        default: return ESP_FAIL;
    }
}

esp_err_t spi_bridge_master_init(void) {
    if (!spi_mutex) {
        spi_mutex = xSemaphoreCreateMutex();
    }
    return spi_bridge_phy_init();
}

void spi_bridge_register_stream_cb(spi_id_t id, spi_stream_cb_t cb) {
    if (id == SPI_ID_WIFI_APP_SNIFFER) stream_cb_wifi = cb;
    if (id == SPI_ID_BT_APP_SNIFFER) stream_cb_bt = cb;

    if ((stream_cb_wifi || stream_cb_bt) && stream_task_handle == NULL) {
        if (!spi_mutex) {
            spi_mutex = xSemaphoreCreateMutex();
        }
        BaseType_t created = xTaskCreate(spi_bridge_stream_task, "spi_stream", 4096, NULL, 5, &stream_task_handle);
        if (created != pdPASS) {
            stream_task_handle = NULL;
            ESP_LOGE(TAG, "Failed to create SPI stream task");
        }
    }
}

void spi_bridge_unregister_stream_cb(spi_id_t id) {
    if (id == SPI_ID_WIFI_APP_SNIFFER) stream_cb_wifi = NULL;
    if (id == SPI_ID_BT_APP_SNIFFER) stream_cb_bt = NULL;
}

static spi_stream_cb_t get_stream_cb(spi_id_t id) {
    if (id == SPI_ID_WIFI_APP_SNIFFER) return stream_cb_wifi;
    if (id == SPI_ID_BT_APP_SNIFFER) return stream_cb_bt;
    return NULL;
}

static esp_err_t spi_bridge_fetch_stream(spi_header_t *out_header, uint8_t *out_payload, uint8_t *out_len) {
    spi_header_t header = {
        .sync = SPI_SYNC_BYTE,
        .type = SPI_TYPE_CMD,
        .id = SPI_ID_SYSTEM_STREAM,
        .length = 0
    };

    uint8_t tx_buf[SPI_FRAME_SIZE];
    uint8_t rx_buf[SPI_FRAME_SIZE];
    memset(tx_buf, 0, sizeof(tx_buf));
    memcpy(tx_buf, &header, sizeof(header));

    esp_err_t ret = spi_bridge_phy_transmit(tx_buf, NULL, SPI_FRAME_SIZE);
    if (ret != ESP_OK) return ret;

    ret = spi_bridge_phy_wait_irq(100);
    if (ret != ESP_OK) return ret;

    memset(tx_buf, 0, sizeof(tx_buf));
    memset(rx_buf, 0, sizeof(rx_buf));
    ret = spi_bridge_phy_transmit(tx_buf, rx_buf, SPI_FRAME_SIZE);
    if (ret != ESP_OK) return ret;

    spi_header_t *resp = (spi_header_t *)rx_buf;
    if (resp->sync != SPI_SYNC_BYTE) return ESP_ERR_INVALID_RESPONSE;

    if (resp->type == SPI_TYPE_STREAM) {
        if (out_header) *out_header = *resp;
        if (out_len) *out_len = resp->length;
        if (resp->length > 0 && out_payload) {
            memcpy(out_payload, rx_buf + sizeof(spi_header_t), resp->length);
        }
        return ESP_OK;
    }

    if (resp->type == SPI_TYPE_RESP && resp->length >= SPI_RESP_STATUS_SIZE) {
        spi_status_t status = (spi_status_t)rx_buf[sizeof(spi_header_t)];
        return spi_status_to_err(status);
    }

    return ESP_ERR_INVALID_RESPONSE;
}

static void spi_bridge_stream_task(void *arg) {
    uint8_t payload[SPI_MAX_PAYLOAD];
    spi_header_t header;

    while (1) {
        if (!stream_cb_wifi && !stream_cb_bt) {
            stream_task_handle = NULL;
            vTaskDelete(NULL);
            return;
        }

        if (command_in_flight) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        uint8_t len = 0;
        esp_err_t ret = spi_bridge_fetch_stream(&header, payload, &len);
        xSemaphoreGive(spi_mutex);
        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        spi_stream_cb_t cb = get_stream_cb(header.id);
        if (cb) {
            cb(header.id, payload, len);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

esp_err_t spi_bridge_send_command(spi_id_t id, const uint8_t *payload, uint8_t len, 
                                 spi_header_t *out_header, uint8_t *out_payload, 
                                 uint32_t timeout_ms) {
    if (!spi_mutex) {
        spi_mutex = xSemaphoreCreateMutex();
    }
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    command_in_flight = true;

    spi_header_t header = {
        .sync = SPI_SYNC_BYTE,
        .type = SPI_TYPE_CMD,
        .id = id,
        .length = len
    };

    if (len > SPI_MAX_PAYLOAD) {
        command_in_flight = false;
        xSemaphoreGive(spi_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_buf[SPI_FRAME_SIZE];
    uint8_t rx_buf[SPI_FRAME_SIZE];
    memset(tx_buf, 0, sizeof(tx_buf));
    memcpy(tx_buf, &header, sizeof(header));
    if (payload && len > 0) {
        memcpy(tx_buf + sizeof(header), payload, len);
    }

    esp_err_t ret = spi_bridge_phy_transmit(tx_buf, NULL, SPI_FRAME_SIZE);
    if (ret != ESP_OK) {
        command_in_flight = false;
        xSemaphoreGive(spi_mutex);
        return ret;
    }

    ret = spi_bridge_phy_wait_irq(timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Command 0x%02X timeout", id);
        command_in_flight = false;
        xSemaphoreGive(spi_mutex);
        return ret;
    }

    memset(tx_buf, 0, sizeof(tx_buf));
    memset(rx_buf, 0, sizeof(rx_buf));
    ret = spi_bridge_phy_transmit(tx_buf, rx_buf, SPI_FRAME_SIZE);
    if (ret != ESP_OK) {
        command_in_flight = false;
        xSemaphoreGive(spi_mutex);
        return ret;
    }

    spi_header_t *resp = (spi_header_t *)rx_buf;

    if (resp->sync != SPI_SYNC_BYTE) {
        ESP_LOGE(TAG, "Sync error: 0x%02X", resp->sync);
        command_in_flight = false;
        xSemaphoreGive(spi_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (resp->type != SPI_TYPE_RESP) {
        ESP_LOGE(TAG, "Invalid response type: 0x%02X", resp->type);
        command_in_flight = false;
        xSemaphoreGive(spi_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (resp->id != id) {
        ESP_LOGW(TAG, "Response ID mismatch (req 0x%02X, resp 0x%02X)", id, resp->id);
    }

    if (resp->length > SPI_MAX_PAYLOAD) {
        ESP_LOGE(TAG, "Invalid response length: %u", resp->length);
        command_in_flight = false;
        xSemaphoreGive(spi_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }

    spi_status_t status = SPI_STATUS_ERROR;
    uint8_t data_len = 0;
    if (resp->length >= SPI_RESP_STATUS_SIZE) {
        status = (spi_status_t)rx_buf[sizeof(spi_header_t)];
        data_len = (uint8_t)(resp->length - SPI_RESP_STATUS_SIZE);
    }

    if (out_header) {
        *out_header = *resp;
        out_header->length = data_len;
    }

    if (data_len > 0 && out_payload) {
        memcpy(out_payload, rx_buf + sizeof(spi_header_t) + SPI_RESP_STATUS_SIZE, data_len);
    }

    esp_err_t out = spi_status_to_err(status);
    command_in_flight = false;
    xSemaphoreGive(spi_mutex);
    return out;
}
