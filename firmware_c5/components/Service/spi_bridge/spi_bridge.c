#include "spi_bridge.h"
#include "spi_slave_driver.h"
#include "wifi_dispatcher.h"
#include "bt_dispatcher.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"
#include "signal_monitor.h"
#include "bluetooth_service.h"
#include "deauther_detector.h"
#include "esp_log.h"
#include "esp_system.h"
#include "storage_assets.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SPI_BRIDGE_C5";

static void*    current_data_source = NULL;
static uint16_t current_item_count = 0;
static const uint16_t *current_item_count_ptr = NULL;
static uint8_t  current_item_size = 0;

#define SPI_STREAM_QUEUE_LEN 8
typedef struct {
    spi_id_t id;
    uint8_t len;
    uint8_t data[SPI_MAX_PAYLOAD];
} spi_stream_item_t;

static spi_stream_item_t stream_queue[SPI_STREAM_QUEUE_LEN];
static uint8_t stream_head = 0;
static uint8_t stream_tail = 0;
static uint8_t stream_count = 0;
static bool stream_wifi_sniffer_enabled = false;
static bool stream_bt_sniffer_enabled = false;
static portMUX_TYPE stream_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool restart_pending = false;
static char s_firmware_version[32] = "unknown";

static void load_firmware_version(void) {
  size_t size;
  uint8_t *json_data = storage_assets_load_file("config/OTA/firmware.json", &size);
  if (json_data == NULL) return;

  cJSON *root = cJSON_ParseWithLength((const char *)json_data, size);
  free(json_data);
  if (root == NULL) return;

  cJSON *version = cJSON_GetObjectItem(root, "version");
  if (cJSON_IsString(version) && version->valuestring != NULL) {
    strncpy(s_firmware_version, version->valuestring, sizeof(s_firmware_version) - 1);
    s_firmware_version[sizeof(s_firmware_version) - 1] = '\0';
    ESP_LOGI(TAG, "Firmware version: %s", s_firmware_version);
  }
  cJSON_Delete(root);
}

void spi_bridge_provide_results(void* source, uint16_t count, uint8_t item_size) {
    current_data_source = source;
    current_item_count = count;
    current_item_count_ptr = NULL;
    current_item_size = item_size;
}

void spi_bridge_provide_results_dynamic(void* source, const uint16_t *count_ptr, uint8_t item_size) {
    current_data_source = source;
    current_item_count = 0;
    current_item_count_ptr = count_ptr;
    current_item_size = item_size;
}

bool spi_bridge_stream_is_enabled(spi_id_t id) {
    if (id == SPI_ID_WIFI_APP_SNIFFER) return stream_wifi_sniffer_enabled;
    if (id == SPI_ID_BT_APP_SNIFFER) return stream_bt_sniffer_enabled;
    return false;
}

void spi_bridge_stream_enable(spi_id_t id, bool enable) {
    if (id == SPI_ID_WIFI_APP_SNIFFER) stream_wifi_sniffer_enabled = enable;
    if (id == SPI_ID_BT_APP_SNIFFER) stream_bt_sniffer_enabled = enable;
}

bool spi_bridge_stream_push(spi_id_t id, const uint8_t *data, uint8_t len) {
    if (!spi_bridge_stream_is_enabled(id)) return false;
    if (!data || len == 0) return false;
    if (len > SPI_MAX_PAYLOAD) return false;

    portENTER_CRITICAL(&stream_mux);
    if (stream_count >= SPI_STREAM_QUEUE_LEN) {
        portEXIT_CRITICAL(&stream_mux);
        return false;
    }
    spi_stream_item_t *item = &stream_queue[stream_tail];
    item->id = id;
    item->len = len;
    memcpy(item->data, data, len);
    stream_tail = (uint8_t)((stream_tail + 1) % SPI_STREAM_QUEUE_LEN);
    stream_count++;
    portEXIT_CRITICAL(&stream_mux);

    return true;
}

static bool spi_bridge_stream_pop(spi_id_t *out_id, uint8_t *out_data, uint8_t *out_len) {
    bool ok = false;
    portENTER_CRITICAL(&stream_mux);
    if (stream_count > 0) {
        spi_stream_item_t *item = &stream_queue[stream_head];
        if (out_id) *out_id = item->id;
        if (out_len) *out_len = item->len;
        if (out_data && item->len > 0) {
            memcpy(out_data, item->data, item->len);
        }
        stream_head = (uint8_t)((stream_head + 1) % SPI_STREAM_QUEUE_LEN);
        stream_count--;
        ok = true;
    }
    portEXIT_CRITICAL(&stream_mux);
    return ok;
}

void spi_bridge_notify_master(void) {
    spi_slave_driver_set_irq(1);
    vTaskDelay(pdMS_TO_TICKS(1)); 
    spi_slave_driver_set_irq(0);
}

static void spi_bridge_task(void *pvParameters) {
    uint8_t rx_buf[SPI_FRAME_SIZE];
    uint8_t tx_buf[SPI_FRAME_SIZE];

    while (1) {
        memset(rx_buf, 0, sizeof(rx_buf));
        if (spi_slave_driver_transmit(NULL, rx_buf, SPI_FRAME_SIZE) != ESP_OK) continue;

        spi_header_t *header = (spi_header_t *)rx_buf;
        if (header->sync != SPI_SYNC_BYTE || header->type != SPI_TYPE_CMD) continue;
        if (header->length > SPI_MAX_PAYLOAD) continue;

        spi_status_t status = SPI_STATUS_OK;
        uint8_t resp_payload[SPI_MAX_PAYLOAD];
        uint8_t resp_len = 0;

        if (header->id == SPI_ID_SYSTEM_PING) {
            status = SPI_STATUS_OK;
        } else if (header->id == SPI_ID_SYSTEM_REBOOT) {
            status = SPI_STATUS_OK;
            restart_pending = true;
        } else if (header->id == SPI_ID_SYSTEM_VERSION) {
            if (strcmp(s_firmware_version, "unknown") == 0) load_firmware_version();
            size_t ver_len = strlen(s_firmware_version);
            if (ver_len > (SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE)) ver_len = (SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE);
            memcpy(resp_payload, s_firmware_version, ver_len);
            resp_len = (uint8_t)ver_len;
            status = SPI_STATUS_OK;
        } else if (header->id == SPI_ID_SYSTEM_STATUS) {
            spi_system_status_t sys = {
                .wifi_active = wifi_service_is_active() ? 1 : 0,
                .wifi_connected = wifi_service_is_connected() ? 1 : 0,
                .bt_running = bluetooth_service_is_running() ? 1 : 0,
                .bt_initialized = bluetooth_service_is_initialized() ? 1 : 0
            };
            memcpy(resp_payload, &sys, sizeof(sys));
            resp_len = sizeof(sys);
            status = SPI_STATUS_OK;
        } else if (header->id == SPI_ID_SYSTEM_DATA) {
            uint16_t index;
            memcpy(&index, rx_buf + sizeof(spi_header_t), 2);
            uint16_t item_count = current_item_count_ptr ? *current_item_count_ptr : current_item_count;

            if (index == SPI_DATA_INDEX_COUNT) { // Pull Count
                memcpy(resp_payload, &item_count, 2);
                resp_len = 2;
            } else if (index == SPI_DATA_INDEX_STATS) { // Pull Sniffer Stats
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
            } else if (index == SPI_DATA_INDEX_DEAUTH_COUNT) {
                uint32_t deauth_count = deauther_detector_get_count();
                memcpy(resp_payload, &deauth_count, sizeof(deauth_count));
                resp_len = sizeof(deauth_count);
            } else if (current_data_source && index < item_count) { // Pull Item
                memcpy(resp_payload, (uint8_t*)current_data_source + (index * current_item_size), current_item_size);
                resp_len = current_item_size;
            } else { status = SPI_STATUS_ERROR; }
        } else if (header->id == SPI_ID_SYSTEM_STREAM) {
            spi_id_t stream_id = 0;
            uint8_t stream_len = 0;
            if (spi_bridge_stream_pop(&stream_id, resp_payload, &stream_len)) {
                spi_header_t stream_header = {.sync = SPI_SYNC_BYTE, .type = SPI_TYPE_STREAM, .id = stream_id, .length = stream_len};
                memset(tx_buf, 0, sizeof(tx_buf));
                memcpy(tx_buf, &stream_header, sizeof(stream_header));
                if (stream_len > 0) memcpy(tx_buf + sizeof(stream_header), resp_payload, stream_len);

                spi_bridge_notify_master();
                spi_slave_driver_transmit(tx_buf, NULL, SPI_FRAME_SIZE);
                if (restart_pending) { vTaskDelay(pdMS_TO_TICKS(50)); esp_restart(); }
                continue;
            }
            status = SPI_STATUS_BUSY;
        } else if (header->id >= 0x10 && header->id <= 0x4F) {
            status = wifi_dispatcher_execute(header->id, rx_buf + sizeof(spi_header_t), header->length, resp_payload, &resp_len);
        } else if (header->id >= 0x50 && header->id <= 0x7F) {
            status = bt_dispatcher_execute(header->id, rx_buf + sizeof(spi_header_t), header->length, resp_payload, &resp_len);
        } else {
            status = SPI_STATUS_UNSUPPORTED;
        }

        if (resp_len > (SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE)) {
            resp_len = 0;
            status = SPI_STATUS_ERROR;
        }

        spi_header_t resp_header = {.sync = SPI_SYNC_BYTE, .type = SPI_TYPE_RESP, .id = header->id, .length = (uint8_t)(resp_len + SPI_RESP_STATUS_SIZE)};
        memset(tx_buf, 0, sizeof(tx_buf));
        memcpy(tx_buf, &resp_header, sizeof(resp_header));
        tx_buf[sizeof(resp_header)] = (uint8_t)status;
        if (resp_len > 0) memcpy(tx_buf + sizeof(resp_header) + SPI_RESP_STATUS_SIZE, resp_payload, resp_len);
        
        spi_bridge_notify_master();
        spi_slave_driver_transmit(tx_buf, NULL, SPI_FRAME_SIZE);

        if (restart_pending) {
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();
        }
    }
}

esp_err_t spi_bridge_slave_init(void) {
    esp_err_t ret = spi_slave_driver_init();
    if (ret != ESP_OK) return ret;
    xTaskCreate(spi_bridge_task, "spi_bridge_task", 4096, NULL, 10, NULL);
    return ESP_OK;
}
