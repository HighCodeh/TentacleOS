#include "wifi_service.h"
#include "spi_bridge.h"
#include <string.h>

static wifi_ap_record_t cached_record; 
static char connected_ssid[33] = {0};

void wifi_init(void) {}

void wifi_service_scan(void) {
    spi_bridge_send_command(SPI_ID_WIFI_SCAN, NULL, 0, NULL, NULL, 10000);
}

uint16_t wifi_service_get_ap_count(void) {
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

wifi_ap_record_t* wifi_service_get_ap_record(uint16_t index) {
    spi_header_t resp;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&index, 2, &resp, (uint8_t*)&cached_record, 1000) == ESP_OK) {
        return &cached_record;
    }
    return NULL;
}

esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password) {
    uint8_t payload[96] = {0};
    strncpy((char*)payload, ssid, 32);
    if (password) strncpy((char*)payload + 32, password, 64);
    return spi_bridge_send_command(SPI_ID_WIFI_CONNECT, payload, 96, NULL, NULL, 15000);
}

bool wifi_service_is_connected(void) {
    return (wifi_service_get_connected_ssid() != NULL);
}

const char* wifi_service_get_connected_ssid(void) {
    spi_header_t resp;
    if (spi_bridge_send_command(SPI_ID_WIFI_GET_STA_INFO, NULL, 0, &resp, (uint8_t*)connected_ssid, 1000) == ESP_OK) {
        connected_ssid[32] = '\0';
        return connected_ssid;
    }
    return NULL;
}

void wifi_stop(void) { spi_bridge_send_command(SPI_ID_WIFI_DISCONNECT, NULL, 0, NULL, NULL, 2000); }
esp_err_t wifi_set_ap_ssid(const char *ssid) { return spi_bridge_send_command(SPI_ID_WIFI_SET_AP, (uint8_t*)ssid, strlen(ssid), NULL, NULL, 2000); }

// Stubs for remaining functions
void wifi_deinit(void) {}
void wifi_start(void) {}
bool wifi_service_is_active(void) { return true; }
void wifi_change_to_hotspot(const char *new_ssid) { wifi_set_ap_ssid(new_ssid); }
esp_err_t wifi_save_ap_config(const char *ssid, const char *password, uint8_t max_conn, const char *ip_addr, bool enabled) { return ESP_OK; }
esp_err_t wifi_set_wifi_enabled(bool enabled) { return ESP_OK; }
esp_err_t wifi_set_ap_password(const char *password) { return ESP_OK; }
esp_err_t wifi_set_ap_max_conn(uint8_t max_conn) { return ESP_OK; }
esp_err_t wifi_set_ap_ip(const char *ip_addr) { return ESP_OK; }
void wifi_service_promiscuous_start(wifi_promiscuous_cb_t cb, wifi_promiscuous_filter_t *filter) {}
void wifi_service_promiscuous_stop(void) {}
void wifi_service_start_channel_hopping(void) {}
void wifi_service_stop_channel_hopping(void) {}
