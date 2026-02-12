#include "bluetooth_service.h"
#include "spi_bridge.h"
#include <string.h>

static ble_scan_result_t cached_result;

esp_err_t bluetooth_service_init(void) { return ESP_OK; }
esp_err_t bluetooth_service_start(void) { return ESP_OK; }

void bluetooth_service_scan(uint32_t duration_ms) {
    spi_bridge_send_command(SPI_ID_BT_SCAN, (uint8_t*)&duration_ms, 4, NULL, NULL, duration_ms + 2000);
}

uint16_t bluetooth_service_get_scan_count(void) {
    spi_header_t resp;
    uint8_t payload[2];
    uint16_t magic_count = 0xFFFF;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_count, 2, &resp, payload, 1000) == ESP_OK) {
        uint16_t count;
        memcpy(&count, payload, 2);
        return count;
    }
    return 0;
}

ble_scan_result_t* bluetooth_service_get_scan_result(uint16_t index) {
    spi_header_t resp;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&index, 2, &resp, (uint8_t*)&cached_result, 1000) == ESP_OK) {
        return &cached_result;
    }
    return NULL;
}

esp_err_t bluetooth_service_connect(const uint8_t *addr, uint8_t addr_type, int (*cb)(struct ble_gap_event *event, void *arg)) {
    uint8_t payload[7];
    memcpy(payload, addr, 6);
    payload[6] = addr_type;
    return spi_bridge_send_command(SPI_ID_BT_CONNECT, payload, 7, NULL, NULL, 5000);
}

void bluetooth_service_get_mac(uint8_t *mac) {
    spi_bridge_send_command(SPI_ID_BT_GET_INFO, NULL, 0, NULL, mac, 1000);
}

void bluetooth_service_disconnect_all(void) {
    spi_bridge_send_command(SPI_ID_BT_DISCONNECT, NULL, 0, NULL, NULL, 2000);
}

// Stubs
esp_err_t bluetooth_service_stop(void) { return ESP_OK; }
esp_err_t bluetooth_service_deinit(void) { return ESP_OK; }
bool bluetooth_service_is_initialized(void) { return true; }
bool bluetooth_service_is_running(void) { return true; }
int bluetooth_service_get_connected_count(void) { return 0; }
esp_err_t bluetooth_service_set_random_mac(void) { return ESP_OK; }
esp_err_t bluetooth_service_start_sniffer(ble_sniffer_cb_t cb) { return ESP_OK; }
void bluetooth_service_stop_sniffer(void) {}
esp_err_t bluetooth_service_start_tracker(const uint8_t *addr, ble_tracker_cb_t cb) { return ESP_OK; }
void bluetooth_service_stop_tracker(void) {}
esp_err_t bluetooth_service_start_advertising(void) { return ESP_OK; }
esp_err_t bluetooth_service_stop_advertising(void) { return ESP_OK; }
uint8_t bluetooth_service_get_own_addr_type(void) { return 0; }
esp_err_t bluetooth_service_set_max_power(void) { return ESP_OK; }
esp_err_t bluetooth_save_announce_config(const char *name, uint8_t max_conn) { return ESP_OK; }
esp_err_t bluetooth_load_spam_list(char ***list, size_t *count) { return ESP_OK; }
esp_err_t bluetooth_save_spam_list(const char * const *list, size_t count) { return ESP_OK; }
void bluetooth_free_spam_list(char **list, size_t count) {}
