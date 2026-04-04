#include "bluetooth_service.h"
#include "spi_bridge.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "BT_SERVICE_P4";
static ble_sniffer_cb_t sniffer_cb = NULL;

static void bt_sniffer_stream_cb(spi_id_t id, const uint8_t *payload, uint8_t len) {
    (void)id;
    if (!sniffer_cb || !payload || len < sizeof(spi_ble_sniffer_frame_t)) return;
    const spi_ble_sniffer_frame_t *frame = (const spi_ble_sniffer_frame_t *)payload;
    sniffer_cb(frame->addr, frame->addr_type, frame->rssi, frame->data, frame->len);
}

static ble_scan_result_t cached_scan_record;

esp_err_t bluetooth_service_init(void) {
    return spi_bridge_send_command(SPI_ID_BT_INIT, NULL, 0, NULL, NULL, 2000);
}

esp_err_t bluetooth_service_deinit(void) {
    return spi_bridge_send_command(SPI_ID_BT_DEINIT, NULL, 0, NULL, NULL, 2000);
}

esp_err_t bluetooth_service_start(void) {
    return spi_bridge_send_command(SPI_ID_BT_START, NULL, 0, NULL, NULL, 2000);
}

esp_err_t bluetooth_service_stop(void) {
    return spi_bridge_send_command(SPI_ID_BT_STOP, NULL, 0, NULL, NULL, 2000);
}

static bool bluetooth_get_info(spi_bt_info_t *out_info) {
    if (!out_info) return false;
    spi_header_t resp;
    if (spi_bridge_send_command(SPI_ID_BT_GET_INFO, NULL, 0, &resp, (uint8_t*)out_info, 1000) == ESP_OK) {
        return true;
    }
    return false;
}

bool bluetooth_service_is_initialized(void) {
    spi_bt_info_t info = {0};
    return bluetooth_get_info(&info) ? (info.initialized != 0) : false;
}

bool bluetooth_service_is_running(void) {
    spi_bt_info_t info = {0};
    return bluetooth_get_info(&info) ? (info.running != 0) : false;
}

void bluetooth_service_disconnect_all(void) {
    spi_bridge_send_command(SPI_ID_BT_DISCONNECT, NULL, 0, NULL, NULL, 2000);
}

int bluetooth_service_get_connected_count(void) {
    spi_bt_info_t info = {0};
    return bluetooth_get_info(&info) ? (int)info.connected_count : 0;
}

void bluetooth_service_get_mac(uint8_t *mac) {
    if (!mac) return;
    spi_bt_info_t info = {0};
    if (bluetooth_get_info(&info)) {
        memcpy(mac, info.mac, 6);
    } else {
        memset(mac, 0, 6);
    }
}

esp_err_t bluetooth_service_set_random_mac(void) {
    return spi_bridge_send_command(SPI_ID_BT_SET_RANDOM_MAC, NULL, 0, NULL, NULL, 2000);
}

esp_err_t bluetooth_service_connect(const uint8_t *addr, uint8_t addr_type, int (*cb)(struct ble_gap_event *event, void *arg)) {
    (void)cb;
    if (!addr) return ESP_ERR_INVALID_ARG;
    uint8_t payload[7];
    memcpy(payload, addr, 6);
    payload[6] = addr_type;
    return spi_bridge_send_command(SPI_ID_BT_CONNECT, payload, sizeof(payload), NULL, NULL, 5000);
}

esp_err_t bluetooth_service_start_sniffer(ble_sniffer_cb_t cb) {
    sniffer_cb = cb;
    if (cb) {
        spi_bridge_register_stream_cb(SPI_ID_BT_APP_SNIFFER, bt_sniffer_stream_cb);
    }
    esp_err_t err = spi_bridge_send_command(SPI_ID_BT_APP_SNIFFER, NULL, 0, NULL, NULL, 2000);
    if (err != ESP_OK) {
        spi_bridge_unregister_stream_cb(SPI_ID_BT_APP_SNIFFER);
        sniffer_cb = NULL;
    }
    return err;
}

void bluetooth_service_stop_sniffer(void) {
    spi_bridge_send_command(SPI_ID_BT_APP_STOP, NULL, 0, NULL, NULL, 2000);
    spi_bridge_unregister_stream_cb(SPI_ID_BT_APP_SNIFFER);
    sniffer_cb = NULL;
}

esp_err_t bluetooth_service_start_tracker(const uint8_t *addr, ble_tracker_cb_t cb) {
    (void)cb;
    if (!addr) return ESP_ERR_INVALID_ARG;
    return spi_bridge_send_command(SPI_ID_BT_TRACKER_START, addr, 6, NULL, NULL, 2000);
}

void bluetooth_service_stop_tracker(void) {
    spi_bridge_send_command(SPI_ID_BT_TRACKER_STOP, NULL, 0, NULL, NULL, 2000);
}

esp_err_t bluetooth_service_start_advertising(void) {
    return spi_bridge_send_command(SPI_ID_BT_START_ADV, NULL, 0, NULL, NULL, 2000);
}

esp_err_t bluetooth_service_stop_advertising(void) {
    return spi_bridge_send_command(SPI_ID_BT_STOP_ADV, NULL, 0, NULL, NULL, 2000);
}

uint8_t bluetooth_service_get_own_addr_type(void) {
    spi_header_t resp;
    uint8_t payload[1] = {0};
    if (spi_bridge_send_command(SPI_ID_BT_GET_ADDR_TYPE, NULL, 0, &resp, payload, 1000) == ESP_OK) {
        return payload[0];
    }
    return 0;
}

esp_err_t bluetooth_service_set_max_power(void) {
    return spi_bridge_send_command(SPI_ID_BT_SET_MAX_POWER, NULL, 0, NULL, NULL, 2000);
}

esp_err_t bluetooth_save_announce_config(const char *name, uint8_t max_conn) {
    if (!name) return ESP_ERR_INVALID_ARG;
    spi_bt_announce_config_t cfg = {0};
    strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    cfg.max_conn = max_conn;
    return spi_bridge_send_command(SPI_ID_BT_SAVE_ANNOUNCE_CFG, (uint8_t*)&cfg, sizeof(cfg), NULL, NULL, 2000);
}

esp_err_t bluetooth_load_spam_list(char ***list, size_t *count) {
    if (!list || !count) return ESP_ERR_INVALID_ARG;
    *list = NULL;
    *count = 0;

    esp_err_t err = spi_bridge_send_command(SPI_ID_BT_SPAM_LIST_LOAD, NULL, 0, NULL, NULL, 2000);
    if (err != ESP_OK) return err;

    spi_header_t resp;
    uint8_t payload[2];
    uint16_t magic_count = SPI_DATA_INDEX_COUNT;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_count, 2, &resp, payload, 1000) != ESP_OK) {
        return ESP_FAIL;
    }

    uint16_t total = 0;
    memcpy(&total, payload, 2);
    if (total == 0) {
        return ESP_OK;
    }

    if (total > SPI_BT_SPAM_LIST_MAX) {
        total = SPI_BT_SPAM_LIST_MAX;
    }

    char **out = (char **)calloc(total, sizeof(char *));
    if (!out) return ESP_ERR_NO_MEM;

    for (uint16_t i = 0; i < total; i++) {
        uint16_t idx = i;
        uint8_t item_buf[SPI_BT_SPAM_ITEM_LEN] = {0};
        if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&idx, 2, &resp, item_buf, 1000) != ESP_OK) {
            bluetooth_free_spam_list(out, i);
            return ESP_FAIL;
        }
        item_buf[SPI_BT_SPAM_ITEM_LEN - 1] = '\0';
        out[i] = strdup((char*)item_buf);
        if (!out[i]) {
            bluetooth_free_spam_list(out, i);
            return ESP_ERR_NO_MEM;
        }
    }

    *list = out;
    *count = total;
    return ESP_OK;
}

esp_err_t bluetooth_save_spam_list(const char * const *list, size_t count) {
    if (!list || count == 0) return ESP_ERR_INVALID_ARG;
    if (count > SPI_BT_SPAM_LIST_MAX) return ESP_ERR_INVALID_ARG;

    uint16_t total = (uint16_t)count;
    esp_err_t err = spi_bridge_send_command(SPI_ID_BT_SPAM_LIST_BEGIN, (uint8_t*)&total, sizeof(total), NULL, NULL, 2000);
    if (err != ESP_OK) return err;

    for (uint16_t i = 0; i < total; i++) {
        uint8_t payload[2 + SPI_BT_SPAM_ITEM_LEN];
        memcpy(payload, &i, 2);
        memset(payload + 2, 0, SPI_BT_SPAM_ITEM_LEN);
        if (list[i]) {
            strncpy((char*)(payload + 2), list[i], SPI_BT_SPAM_ITEM_LEN - 1);
        }
        err = spi_bridge_send_command(SPI_ID_BT_SPAM_LIST_ITEM, payload, sizeof(payload), NULL, NULL, 2000);
        if (err != ESP_OK) return err;
    }

    return spi_bridge_send_command(SPI_ID_BT_SPAM_LIST_COMMIT, NULL, 0, NULL, NULL, 2000);
}

void bluetooth_free_spam_list(char **list, size_t count) {
    if (!list) return;
    for (size_t i = 0; i < count; i++) free(list[i]);
    free(list);
}

void bluetooth_service_scan(uint32_t duration_ms) {
    uint8_t payload[4];
    memcpy(payload, &duration_ms, sizeof(duration_ms));
    spi_bridge_send_command(SPI_ID_BT_SCAN, payload, sizeof(payload), NULL, NULL, 10000);
}

uint16_t bluetooth_service_get_scan_count(void) {
    spi_header_t resp;
    uint8_t payload[2];
    uint16_t magic_count = SPI_DATA_INDEX_COUNT;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_count, 2, &resp, payload, 1000) == ESP_OK) {
        uint16_t count;
        memcpy(&count, payload, 2);
        return count;
    }
    return 0;
}

ble_scan_result_t* bluetooth_service_get_scan_result(uint16_t index) {
    spi_header_t resp;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&index, 2, &resp, (uint8_t*)&cached_scan_record, 1000) == ESP_OK) {
        return &cached_scan_record;
    }
    return NULL;
}
