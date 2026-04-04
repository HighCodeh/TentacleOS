#include "wifi_service.h"
#include "spi_bridge.h"
#include "esp_log.h"
#include <string.h>

static wifi_ap_record_t cached_record; 
static char connected_ssid[33] = {0};
static const char *TAG = "WIFI_SERVICE_P4";

void wifi_init(void) { spi_bridge_send_command(SPI_ID_WIFI_START, NULL, 0, NULL, NULL, 2000); }

void wifi_service_scan(void) { spi_bridge_send_command(SPI_ID_WIFI_SCAN, NULL, 0, NULL, NULL, 20000); }

uint16_t wifi_service_get_ap_count(void) {
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

wifi_ap_record_t* wifi_service_get_ap_record(uint16_t index) {
    spi_header_t resp;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&index, 2, &resp, (uint8_t*)&cached_record, 1000) == ESP_OK) {
        return &cached_record;
    }
    return NULL;
}

esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password) {
    spi_wifi_connect_t cfg = {0};
    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid));
    if (password) strncpy(cfg.password, password, sizeof(cfg.password));
    return spi_bridge_send_command(SPI_ID_WIFI_CONNECT, (uint8_t*)&cfg, sizeof(cfg), NULL, NULL, 15000);
}

bool wifi_service_is_connected(void) {
    spi_header_t resp;
    spi_system_status_t sys = {0};
    if (spi_bridge_send_command(SPI_ID_SYSTEM_STATUS, NULL, 0, &resp, (uint8_t*)&sys, 1000) == ESP_OK) {
        return sys.wifi_connected != 0;
    }
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

void wifi_stop(void) { spi_bridge_send_command(SPI_ID_WIFI_STOP, NULL, 0, NULL, NULL, 2000); }
esp_err_t wifi_set_ap_ssid(const char *ssid) { return spi_bridge_send_command(SPI_ID_WIFI_SET_AP, (uint8_t*)ssid, strlen(ssid), NULL, NULL, 2000); }

// Stubs for remaining functions
void wifi_deinit(void) { spi_bridge_send_command(SPI_ID_WIFI_STOP, NULL, 0, NULL, NULL, 2000); }
void wifi_start(void) { spi_bridge_send_command(SPI_ID_WIFI_START, NULL, 0, NULL, NULL, 2000); }
bool wifi_service_is_active(void) {
    spi_header_t resp;
    spi_system_status_t sys = {0};
    if (spi_bridge_send_command(SPI_ID_SYSTEM_STATUS, NULL, 0, &resp, (uint8_t*)&sys, 1000) == ESP_OK) {
        return sys.wifi_active != 0;
    }
    return false;
}
void wifi_change_to_hotspot(const char *new_ssid) { wifi_set_ap_ssid(new_ssid); }
esp_err_t wifi_save_ap_config(const char *ssid, const char *password, uint8_t max_conn, const char *ip_addr, bool enabled) {
    spi_wifi_ap_config_t cfg = {0};
    if (!ssid) return ESP_ERR_INVALID_ARG;
    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid));
    if (password) strncpy(cfg.password, password, sizeof(cfg.password));
    cfg.max_conn = max_conn;
    if (ip_addr) strncpy(cfg.ip_addr, ip_addr, sizeof(cfg.ip_addr));
    cfg.enabled = enabled ? 1 : 0;
    return spi_bridge_send_command(SPI_ID_WIFI_SAVE_AP_CONFIG, (uint8_t*)&cfg, sizeof(cfg), NULL, NULL, 2000);
}
esp_err_t wifi_set_wifi_enabled(bool enabled) {
    uint8_t payload = enabled ? 1 : 0;
    return spi_bridge_send_command(SPI_ID_WIFI_SET_ENABLED, &payload, 1, NULL, NULL, 2000);
}
esp_err_t wifi_set_ap_password(const char *password) {
    if (!password) return ESP_ERR_INVALID_ARG;
    return spi_bridge_send_command(SPI_ID_WIFI_SET_AP_PASSWORD, (uint8_t*)password, strlen(password), NULL, NULL, 2000);
}
esp_err_t wifi_set_ap_max_conn(uint8_t max_conn) {
    uint8_t payload = max_conn;
    return spi_bridge_send_command(SPI_ID_WIFI_SET_AP_MAX_CONN, &payload, 1, NULL, NULL, 2000);
}
esp_err_t wifi_set_ap_ip(const char *ip_addr) {
    if (!ip_addr) return ESP_ERR_INVALID_ARG;
    return spi_bridge_send_command(SPI_ID_WIFI_SET_AP_IP, (uint8_t*)ip_addr, strlen(ip_addr), NULL, NULL, 2000);
}
void wifi_service_promiscuous_start(wifi_promiscuous_cb_t cb, wifi_promiscuous_filter_t *filter) {
    (void)cb; (void)filter;
    esp_err_t err = spi_bridge_send_command(SPI_ID_WIFI_PROMISC_START, NULL, 0, NULL, NULL, 2000);
    if (err == ESP_ERR_NOT_SUPPORTED) ESP_LOGW(TAG, "Promiscuous start not supported over SPI");
}
void wifi_service_promiscuous_stop(void) {
    esp_err_t err = spi_bridge_send_command(SPI_ID_WIFI_PROMISC_STOP, NULL, 0, NULL, NULL, 2000);
    if (err == ESP_ERR_NOT_SUPPORTED) ESP_LOGW(TAG, "Promiscuous stop not supported over SPI");
}
void wifi_service_start_channel_hopping(void) {
    spi_bridge_send_command(SPI_ID_WIFI_CH_HOP_START, NULL, 0, NULL, NULL, 2000);
}
void wifi_service_stop_channel_hopping(void) {
    spi_bridge_send_command(SPI_ID_WIFI_CH_HOP_STOP, NULL, 0, NULL, NULL, 2000);
}
