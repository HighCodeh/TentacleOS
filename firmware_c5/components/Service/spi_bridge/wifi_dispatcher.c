#include "wifi_dispatcher.h"
#include "wifi_service.h"
#include "ap_scanner.h"
#include "client_scanner.h"
#include "beacon_spam.h"
#include "wifi_deauther.h"
#include "wifi_flood.h"
#include "wifi_sniffer.h"
#include "evil_twin.h"
#include "deauther_detector.h"
#include "probe_monitor.h"
#include "signal_monitor.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WIFI_DISPATCHER";

spi_status_t wifi_dispatcher_execute(spi_id_t id, const uint8_t *payload, uint8_t len, 
                                   uint8_t *out_resp_payload, uint8_t *out_resp_len) {
    *out_resp_len = 0;
    
    switch (id) {
        case SPI_ID_WIFI_SCAN:
            wifi_service_scan();
            // Point bridge to WiFi result set
            spi_bridge_provide_results(wifi_service_get_ap_record(0), wifi_service_get_ap_count(), sizeof(wifi_ap_record_t));
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_CONNECT: {
            if (len < 32) return SPI_STATUS_ERROR;
            char ssid[33] = {0};
            char pass[65] = {0};
            memcpy(ssid, payload, 32);
            if (len > 32) memcpy(pass, payload + 32, len - 32);
            return (wifi_service_connect_to_ap(ssid, pass) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_DISCONNECT:
            esp_wifi_disconnect();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_GET_STA_INFO: {
            const char* ssid = wifi_service_get_connected_ssid();
            if (ssid) {
                strncpy((char*)out_resp_payload, ssid, 32);
                *out_resp_len = 32;
                return SPI_STATUS_OK;
            }
            return SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_APP_SCAN_AP:
            if (ap_scanner_start()) {
                uint16_t count;
                spi_bridge_provide_results(ap_scanner_get_results(&count), count, sizeof(wifi_ap_record_t));
                return SPI_STATUS_OK;
            }
            return SPI_STATUS_BUSY;

        case SPI_ID_WIFI_APP_SCAN_CLIENT:
            if (client_scanner_start()) {
                uint16_t count;
                spi_bridge_provide_results(client_scanner_get_results(&count), count, sizeof(client_record_t));
                return SPI_STATUS_OK;
            }
            return SPI_STATUS_BUSY;

        case SPI_ID_WIFI_APP_BEACON_SPAM: {
            if (len == 0) return beacon_spam_start_random() ? SPI_STATUS_OK : SPI_STATUS_ERROR;
            return beacon_spam_start_custom((const char*)payload) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_APP_DEAUTHER: {
            if (len < 12) return SPI_STATUS_ERROR;
            wifi_ap_record_t target = {0};
            memcpy(target.bssid, payload, 6);
            uint8_t client[6];
            memcpy(client, payload + 6, 6);
            return wifi_deauther_start_targeted(&target, client, DEAUTH_INVALID_AUTH) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_APP_FLOOD: {
            if (len < 7) return SPI_STATUS_ERROR;
            uint8_t type = payload[0];
            uint8_t bssid[6];
            memcpy(bssid, payload + 1, 6);
            if (type == 0) return wifi_flood_auth_start(bssid, 1) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
            if (type == 1) return wifi_flood_assoc_start(bssid, 1) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
            return SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_APP_SNIFFER: {
            if (len < 2) return SPI_STATUS_ERROR;
            return wifi_sniffer_start((sniff_type_t)payload[0], payload[1]) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_APP_EVIL_TWIN:
            evil_twin_start_attack((const char*)payload);
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_APP_DEAUTH_DET:
            deauther_detector_start();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_APP_PROBE_MON:
            if (probe_monitor_start()) {
                uint16_t count;
                spi_bridge_provide_results(probe_monitor_get_results(&count), count, sizeof(probe_record_t));
                return SPI_STATUS_OK;
            }
            return SPI_STATUS_ERROR;

        case SPI_ID_WIFI_APP_SIGNAL_MON: {
            if (len < 7) return SPI_STATUS_ERROR;
            signal_monitor_start(payload, payload[6]);
            return SPI_STATUS_OK;
        }

        case SPI_ID_WIFI_APP_ATTACK_STOP:
            wifi_deauther_stop();
            wifi_flood_stop();
            wifi_sniffer_stop();
            evil_twin_stop_attack();
            deauther_detector_stop();
            probe_monitor_stop();
            signal_monitor_stop();
            beacon_spam_stop();
            return SPI_STATUS_OK;

        default:
            return SPI_STATUS_ERROR;
    }
}