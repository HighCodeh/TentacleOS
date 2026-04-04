#include "wifi_dispatcher.h"
#include "spi_bridge.h"
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
#include "target_scanner.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "WIFI_DISPATCHER";

static void spi_promisc_noop_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    (void)buf;
    (void)type;
}

spi_status_t wifi_dispatcher_execute(spi_id_t id, const uint8_t *payload, uint8_t len, 
                                   uint8_t *out_resp_payload, uint8_t *out_resp_len) {
    (void)TAG;
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

        case SPI_ID_WIFI_SET_AP: {
            if (len == 0) return SPI_STATUS_INVALID_ARG;
            char ssid[33] = {0};
            uint8_t copy_len = (len > 32) ? 32 : len;
            memcpy(ssid, payload, copy_len);
            return (wifi_set_ap_ssid(ssid) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_START:
            wifi_start();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_STOP:
            wifi_stop();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_SAVE_AP_CONFIG: {
            if (len < sizeof(spi_wifi_ap_config_t)) return SPI_STATUS_INVALID_ARG;
            const spi_wifi_ap_config_t *cfg = (const spi_wifi_ap_config_t *)payload;
            return (wifi_save_ap_config(cfg->ssid, cfg->password, cfg->max_conn, cfg->ip_addr, cfg->enabled) == ESP_OK)
                       ? SPI_STATUS_OK
                       : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_SET_ENABLED:
            if (len < 1) return SPI_STATUS_INVALID_ARG;
            return (wifi_set_wifi_enabled(payload[0] ? true : false) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        case SPI_ID_WIFI_SET_AP_PASSWORD: {
            if (len == 0) return SPI_STATUS_INVALID_ARG;
            char pass[65] = {0};
            uint8_t copy_len = (len > 64) ? 64 : len;
            memcpy(pass, payload, copy_len);
            return (wifi_set_ap_password(pass) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_SET_AP_MAX_CONN:
            if (len < 1) return SPI_STATUS_INVALID_ARG;
            return (wifi_set_ap_max_conn(payload[0]) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        case SPI_ID_WIFI_SET_AP_IP: {
            if (len == 0) return SPI_STATUS_INVALID_ARG;
            char ip_addr[16] = {0};
            uint8_t copy_len = (len > 15) ? 15 : len;
            memcpy(ip_addr, payload, copy_len);
            return (wifi_set_ap_ip(ip_addr) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_PROMISC_START:
            {
                wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_ALL };
                wifi_service_promiscuous_start(spi_promisc_noop_cb, &filter);
                return SPI_STATUS_OK;
            }
        case SPI_ID_WIFI_PROMISC_STOP:
            wifi_service_promiscuous_stop();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_CH_HOP_START:
            wifi_service_start_channel_hopping();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_CH_HOP_STOP:
            wifi_service_stop_channel_hopping();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_APP_SCAN_AP:
            wifi_service_scan();
            spi_bridge_provide_results(wifi_service_get_ap_record(0), wifi_service_get_ap_count(), sizeof(wifi_ap_record_t));
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_APP_SCAN_CLIENT:
            if (!client_scanner_start()) return SPI_STATUS_BUSY;
            {
                const TickType_t start = xTaskGetTickCount();
                const TickType_t timeout = pdMS_TO_TICKS(17000);
                uint16_t count = 0;
                client_record_t *results = NULL;
                while ((results = client_scanner_get_results(&count)) == NULL) {
                    if ((xTaskGetTickCount() - start) > timeout) {
                        return SPI_STATUS_BUSY;
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                spi_bridge_provide_results(results, count, sizeof(client_record_t));
                return SPI_STATUS_OK;
            }

        case SPI_ID_WIFI_APP_BEACON_SPAM: {
            if (len == 0) return beacon_spam_start_random() ? SPI_STATUS_OK : SPI_STATUS_ERROR;
            char path[257] = {0};
            size_t copy_len = len;
            if (copy_len >= sizeof(path)) copy_len = sizeof(path) - 1;
            memcpy(path, payload, copy_len);
            return beacon_spam_start_custom(path) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_APP_DEAUTHER: {
            if (len < 13) return SPI_STATUS_ERROR;
            wifi_ap_record_t target = {0};
            memcpy(target.bssid, payload, 6);
            if (len >= 14) target.primary = payload[13];
            uint8_t client[6];
            memcpy(client, payload + 6, 6);
            deauth_frame_type_t type = (deauth_frame_type_t)payload[12];
            return wifi_deauther_start_targeted(&target, client, type) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_APP_FLOOD: {
            if (len < 7) return SPI_STATUS_ERROR;
            uint8_t type = payload[0];
            uint8_t bssid[6];
            memcpy(bssid, payload + 1, 6);
            uint8_t channel = (len >= 8) ? payload[7] : 1;
            if (type == 0) return wifi_flood_auth_start(bssid, channel) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
            if (type == 1) return wifi_flood_assoc_start(bssid, channel) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
            if (type == 2) return wifi_flood_probe_start(bssid, channel) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
            return SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_APP_SNIFFER: {
            if (len < 2) return SPI_STATUS_ERROR;
            spi_bridge_stream_enable(SPI_ID_WIFI_APP_SNIFFER, true);
            if (wifi_sniffer_start((sniff_type_t)payload[0], payload[1])) {
                return SPI_STATUS_OK;
            }
            spi_bridge_stream_enable(SPI_ID_WIFI_APP_SNIFFER, false);
            return SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_APP_EVIL_TWIN:
            if (len == 0) return SPI_STATUS_INVALID_ARG;
            {
                char ssid[33] = {0};
                uint8_t copy_len = (len > 32) ? 32 : len;
                memcpy(ssid, payload, copy_len);
                evil_twin_start_attack(ssid);
                return SPI_STATUS_OK;
            }

        case SPI_ID_WIFI_APP_DEAUTH_DET:
            deauther_detector_start();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_APP_PROBE_MON:
            if (probe_monitor_start()) {
                uint16_t count;
                probe_record_t *results = probe_monitor_get_results(&count);
                spi_bridge_provide_results_dynamic(results, probe_monitor_get_count_ptr(), sizeof(probe_record_t));
                return SPI_STATUS_OK;
            }
            return SPI_STATUS_BUSY;

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
            spi_bridge_stream_enable(SPI_ID_WIFI_APP_SNIFFER, false);
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_SNIFFER_SET_SNAPLEN: {
            if (len < 2) return SPI_STATUS_INVALID_ARG;
            uint16_t snaplen = 0;
            memcpy(&snaplen, payload, sizeof(uint16_t));
            wifi_sniffer_set_snaplen(snaplen);
            return SPI_STATUS_OK;
        }

        case SPI_ID_WIFI_SNIFFER_SET_VERBOSE:
            if (len < 1) return SPI_STATUS_INVALID_ARG;
            wifi_sniffer_set_verbose(payload[0] != 0);
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_SNIFFER_SAVE_FLASH: {
            if (len < 1) return SPI_STATUS_INVALID_ARG;
            char filename[SPI_WIFI_SNIFFER_FILENAME_MAX] = {0};
            uint8_t copy_len = (len >= SPI_WIFI_SNIFFER_FILENAME_MAX) ? (SPI_WIFI_SNIFFER_FILENAME_MAX - 1) : len;
            memcpy(filename, payload, copy_len);
            return wifi_sniffer_save_to_internal_flash(filename) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_SNIFFER_SAVE_SD: {
            if (len < 1) return SPI_STATUS_INVALID_ARG;
            char filename[SPI_WIFI_SNIFFER_FILENAME_MAX] = {0};
            uint8_t copy_len = (len >= SPI_WIFI_SNIFFER_FILENAME_MAX) ? (SPI_WIFI_SNIFFER_FILENAME_MAX - 1) : len;
            memcpy(filename, payload, copy_len);
            return wifi_sniffer_save_to_sd_card(filename) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_SNIFFER_FREE_BUFFER:
            wifi_sniffer_free_buffer();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_SNIFFER_STREAM_SD: {
            if (len < 3) return SPI_STATUS_INVALID_ARG;
            sniff_type_t type = (sniff_type_t)payload[0];
            uint8_t channel = payload[1];
            char filename[SPI_WIFI_SNIFFER_FILENAME_MAX] = {0};
            uint8_t name_len = (uint8_t)(len - 2);
            uint8_t copy_len = (name_len >= SPI_WIFI_SNIFFER_FILENAME_MAX) ? (SPI_WIFI_SNIFFER_FILENAME_MAX - 1) : name_len;
            memcpy(filename, payload + 2, copy_len);
            return wifi_sniffer_start_stream_sd(type, channel, filename) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_SNIFFER_CLEAR_PMKID:
            wifi_sniffer_clear_pmkid();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_SNIFFER_GET_PMKID_BSSID:
            wifi_sniffer_get_pmkid_bssid(out_resp_payload);
            *out_resp_len = 6;
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_SNIFFER_CLEAR_HANDSHAKE:
            wifi_sniffer_clear_handshake();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_SNIFFER_GET_HANDSHAKE_BSSID:
            wifi_sniffer_get_handshake_bssid(out_resp_payload);
            *out_resp_len = 6;
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_DEAUTH_STATUS:
            out_resp_payload[0] = wifi_deauther_is_running() ? 1 : 0;
            *out_resp_len = 1;
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_DEAUTH_SEND_RAW:
            if (len == 0) return SPI_STATUS_INVALID_ARG;
            wifi_deauther_send_raw_frame(payload, len);
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_ASSOC_REQUEST: {
            if (len < 8) return SPI_STATUS_INVALID_ARG;
            uint8_t ssid_len = payload[7];
            if (ssid_len > 32) return SPI_STATUS_INVALID_ARG;
            if (len < (uint8_t)(8 + ssid_len)) return SPI_STATUS_INVALID_ARG;

            wifi_ap_record_t ap = {0};
            memcpy(ap.bssid, payload, 6);
            ap.primary = payload[6];
            memcpy(ap.ssid, payload + 8, ssid_len);
            ap.ssid[ssid_len] = '\0';

            wifi_send_association_request(&ap);
            return SPI_STATUS_OK;
        }

        case SPI_ID_WIFI_DEAUTH_SEND_FRAME: {
            if (len < 8) return SPI_STATUS_INVALID_ARG;
            wifi_ap_record_t ap = {0};
            memcpy(ap.bssid, payload, 6);
            ap.primary = payload[7];
            deauth_frame_type_t type = (deauth_frame_type_t)payload[6];
            wifi_deauther_send_deauth_frame(&ap, type);
            return SPI_STATUS_OK;
        }

        case SPI_ID_WIFI_DEAUTH_SEND_BROADCAST: {
            if (len < 8) return SPI_STATUS_INVALID_ARG;
            wifi_ap_record_t ap = {0};
            memcpy(ap.bssid, payload, 6);
            ap.primary = payload[7];
            deauth_frame_type_t type = (deauth_frame_type_t)payload[6];
            wifi_deauther_send_broadcast_deauth(&ap, type);
            return SPI_STATUS_OK;
        }

        case SPI_ID_WIFI_TARGET_SCAN_START: {
            if (len < 7) return SPI_STATUS_INVALID_ARG;
            const uint8_t *bssid = payload;
            uint8_t channel = payload[6];
            if (target_scanner_start(bssid, channel)) {
                target_client_record_t *results = target_scanner_get_live_results(NULL, NULL);
                spi_bridge_provide_results_dynamic(results,
                                                   target_scanner_get_count_ptr(),
                                                   sizeof(target_client_record_t));
                return SPI_STATUS_OK;
            }
            return SPI_STATUS_ERROR;
        }

        case SPI_ID_WIFI_TARGET_SCAN_STATUS: {
            uint16_t count = 0;
            (void)target_scanner_get_live_results(&count, NULL);
            out_resp_payload[0] = target_scanner_is_scanning() ? 1 : 0;
            memcpy(out_resp_payload + 1, &count, sizeof(uint16_t));
            *out_resp_len = 3;
            return SPI_STATUS_OK;
        }

        case SPI_ID_WIFI_TARGET_SAVE_FLASH:
            return target_scanner_save_results_to_internal_flash() ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        case SPI_ID_WIFI_TARGET_SAVE_SD:
            return target_scanner_save_results_to_sd_card() ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        case SPI_ID_WIFI_TARGET_FREE:
            target_scanner_free_results();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_PROBE_SAVE_FLASH:
            return probe_monitor_save_results_to_internal_flash() ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        case SPI_ID_WIFI_PROBE_SAVE_SD:
            return probe_monitor_save_results_to_sd_card() ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        case SPI_ID_WIFI_EVIL_TWIN_TEMPLATE: {
            if (len < 2) return SPI_STATUS_INVALID_ARG;
            uint8_t ssid_len = payload[0];
            if (ssid_len > 32) return SPI_STATUS_INVALID_ARG;
            if (len < (uint8_t)(1 + ssid_len + 1)) return SPI_STATUS_INVALID_ARG;
            uint8_t template_len = payload[1 + ssid_len];
            if (len < (uint8_t)(1 + ssid_len + 1 + template_len)) return SPI_STATUS_INVALID_ARG;

            char ssid[33] = {0};
            char template_path[128] = {0};
            memcpy(ssid, payload + 1, ssid_len);
            if (template_len > 0) {
                uint8_t copy_len = (template_len >= sizeof(template_path)) ? (sizeof(template_path) - 1) : template_len;
                memcpy(template_path, payload + 1 + ssid_len + 1, copy_len);
            }
            evil_twin_start_attack_with_template(ssid, template_path);
            return SPI_STATUS_OK;
        }

        case SPI_ID_WIFI_EVIL_TWIN_HAS_PASSWORD:
            out_resp_payload[0] = evil_twin_has_password() ? 1 : 0;
            *out_resp_len = 1;
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_EVIL_TWIN_GET_PASSWORD: {
            char password[64] = {0};
            evil_twin_get_last_password(password, sizeof(password));
            size_t out_len = strnlen(password, sizeof(password));
            if (out_len >= sizeof(password)) out_len = sizeof(password) - 1;
            memcpy(out_resp_payload, password, out_len + 1);
            *out_resp_len = (uint8_t)(out_len + 1);
            return SPI_STATUS_OK;
        }

        case SPI_ID_WIFI_EVIL_TWIN_RESET_CAPTURE:
            evil_twin_reset_capture();
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_EVIL_TWIN_TMPL_BEGIN: {
            if (len < 2) return SPI_STATUS_ERROR;
            uint16_t total_size = (uint16_t)(payload[0] | (payload[1] << 8));
            evil_twin_tmpl_begin(total_size);
            return SPI_STATUS_OK;
        }

        case SPI_ID_WIFI_EVIL_TWIN_TMPL_CHUNK:
            evil_twin_tmpl_chunk(payload, len);
            return SPI_STATUS_OK;

        case SPI_ID_WIFI_CLIENT_SAVE_FLASH:
            return client_scanner_save_results_to_internal_flash() ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        case SPI_ID_WIFI_CLIENT_SAVE_SD:
            return client_scanner_save_results_to_sd_card() ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        case SPI_ID_WIFI_AP_SAVE_FLASH:
            return ap_scanner_save_results_to_internal_flash() ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        case SPI_ID_WIFI_AP_SAVE_SD:
            return ap_scanner_save_results_to_sd_card() ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        default:
            return SPI_STATUS_ERROR;
    }
}
