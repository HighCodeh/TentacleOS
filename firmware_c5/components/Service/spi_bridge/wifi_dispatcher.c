// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "wifi_dispatcher.h"

#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ap_scanner.h"
#include "beacon_spam.h"
#include "client_scanner.h"
#include "deauther_detector.h"
#include "evil_twin.h"
#include "probe_monitor.h"
#include "signal_monitor.h"
#include "spi_bridge.h"
#include "target_scanner.h"
#include "wifi_deauther.h"
#include "wifi_flood.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"

static const char *TAG = "WIFI_DISPATCHER";

#define WIFI_SSID_MAX_LEN         32
#define WIFI_PASSWORD_MAX_LEN     64
#define WIFI_IP_ADDR_MAX_LEN      15
#define WIFI_DEAUTHER_MIN_PAYLOAD 13
#define WIFI_FLOOD_MIN_PAYLOAD    7
#define WIFI_SNIFFER_MIN_PAYLOAD  2
#define WIFI_ASSOC_MIN_PAYLOAD    8
#define WIFI_DEAUTH_FRAME_MIN     8
#define WIFI_TARGET_MIN_PAYLOAD   7
#define WIFI_SIGNAL_MON_MIN_LEN   7
#define WIFI_SNIFFER_STREAM_MIN   3
#define WIFI_EVIL_TWIN_TMPL_MIN   2
#define WIFI_PATH_MAX_LEN         257
#define WIFI_SSID_BUF_LEN         33
#define WIFI_PASS_BUF_LEN         65
#define WIFI_TEMPLATE_PATH_LEN    128
#define WIFI_MAC_LEN              6
#define WIFI_CLIENT_SCAN_TIMEOUT  17000
#define CLIENT_SCAN_POLL_DELAY_MS 100
#define WIFI_FLOOD_TYPE_AUTH      0
#define WIFI_FLOOD_TYPE_ASSOC     1
#define WIFI_FLOOD_TYPE_PROBE     2

static void promisc_noop_cb(void *buf, wifi_promiscuous_pkt_type_t type);

spi_status_t wifi_dispatcher_execute(spi_id_t id,
                                     const uint8_t *payload,
                                     uint8_t len,
                                     uint8_t *out_resp_payload,
                                     uint8_t *out_resp_len) {
  (void)TAG;
  *out_resp_len = 0;

  switch (id) {
    case SPI_ID_WIFI_SCAN:
      wifi_service_scan();
      spi_bridge_provide_results(
          wifi_service_get_ap_record(0), wifi_service_get_ap_count(), sizeof(wifi_ap_record_t));
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_CONNECT: {
      if (len < WIFI_SSID_MAX_LEN)
        return SPI_STATUS_ERROR;
      char ssid[WIFI_SSID_BUF_LEN] = {0};
      char pass[WIFI_PASS_BUF_LEN] = {0};
      memcpy(ssid, payload, WIFI_SSID_MAX_LEN);
      if (len > WIFI_SSID_MAX_LEN)
        memcpy(pass, payload + WIFI_SSID_MAX_LEN, len - WIFI_SSID_MAX_LEN);
      return (wifi_service_connect_to_ap(ssid, pass) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_DISCONNECT:
      esp_wifi_disconnect();
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_GET_STA_INFO: {
      const char *ssid = wifi_service_get_connected_ssid();
      if (ssid != NULL) {
        strncpy((char *)out_resp_payload, ssid, WIFI_SSID_MAX_LEN);
        *out_resp_len = WIFI_SSID_MAX_LEN;
        return SPI_STATUS_OK;
      }
      return SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_SET_AP: {
      if (len == 0)
        return SPI_STATUS_INVALID_ARG;
      char ssid[WIFI_SSID_BUF_LEN] = {0};
      uint8_t copy_len = (len > WIFI_SSID_MAX_LEN) ? WIFI_SSID_MAX_LEN : len;
      memcpy(ssid, payload, copy_len);
      return (wifi_service_set_ap_ssid(ssid) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_START:
      wifi_service_start();
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_STOP:
      wifi_service_stop();
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_SAVE_AP_CONFIG: {
      if (len < sizeof(spi_wifi_ap_config_t))
        return SPI_STATUS_INVALID_ARG;
      const spi_wifi_ap_config_t *cfg = (const spi_wifi_ap_config_t *)payload;
      return (wifi_service_save_ap_config(
                  cfg->ssid, cfg->password, cfg->max_conn, cfg->ip_addr, cfg->enabled) == ESP_OK)
                 ? SPI_STATUS_OK
                 : SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_SET_ENABLED:
      if (len < 1)
        return SPI_STATUS_INVALID_ARG;
      return (wifi_service_set_enabled(payload[0] ? true : false) == ESP_OK) ? SPI_STATUS_OK
                                                                             : SPI_STATUS_ERROR;

    case SPI_ID_WIFI_SET_AP_PASSWORD: {
      if (len == 0)
        return SPI_STATUS_INVALID_ARG;
      char pass[WIFI_PASS_BUF_LEN] = {0};
      uint8_t copy_len = (len > WIFI_PASSWORD_MAX_LEN) ? WIFI_PASSWORD_MAX_LEN : len;
      memcpy(pass, payload, copy_len);
      return (wifi_service_set_ap_password(pass) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_SET_AP_MAX_CONN:
      if (len < 1)
        return SPI_STATUS_INVALID_ARG;
      return (wifi_service_set_ap_max_conn(payload[0]) == ESP_OK) ? SPI_STATUS_OK
                                                                  : SPI_STATUS_ERROR;

    case SPI_ID_WIFI_SET_AP_IP: {
      if (len == 0)
        return SPI_STATUS_INVALID_ARG;
      char ip_addr[16] = {0};
      uint8_t copy_len = (len > WIFI_IP_ADDR_MAX_LEN) ? WIFI_IP_ADDR_MAX_LEN : len;
      memcpy(ip_addr, payload, copy_len);
      return (wifi_service_set_ap_ip(ip_addr) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_PROMISC_START: {
      wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL};
      wifi_service_promiscuous_start(promisc_noop_cb, &filter);
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
      spi_bridge_provide_results(
          wifi_service_get_ap_record(0), wifi_service_get_ap_count(), sizeof(wifi_ap_record_t));
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_APP_SCAN_CLIENT:
      if (!client_scanner_start())
        return SPI_STATUS_BUSY;
      {
        const TickType_t start = xTaskGetTickCount();
        const TickType_t timeout = pdMS_TO_TICKS(WIFI_CLIENT_SCAN_TIMEOUT);
        uint16_t count = 0;
        client_scanner_record_t *results = NULL;
        while ((results = client_scanner_get_results(&count)) == NULL) {
          if ((xTaskGetTickCount() - start) > timeout) {
            return SPI_STATUS_BUSY;
          }
          vTaskDelay(pdMS_TO_TICKS(CLIENT_SCAN_POLL_DELAY_MS));
        }
        spi_bridge_provide_results(results, count, sizeof(client_scanner_record_t));
        return SPI_STATUS_OK;
      }

    case SPI_ID_WIFI_APP_BEACON_SPAM: {
      if (len == 0)
        return beacon_spam_start_random() ? SPI_STATUS_OK : SPI_STATUS_ERROR;
      char path[WIFI_PATH_MAX_LEN] = {0};
      size_t copy_len = len;
      if (copy_len >= sizeof(path))
        copy_len = sizeof(path) - 1;
      memcpy(path, payload, copy_len);
      return beacon_spam_start_custom(path) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_APP_DEAUTHER: {
      if (len < WIFI_DEAUTHER_MIN_PAYLOAD)
        return SPI_STATUS_ERROR;
      wifi_ap_record_t target = {0};
      memcpy(target.bssid, payload, WIFI_MAC_LEN);
      if (len >= 14)
        target.primary = payload[13];
      uint8_t client[WIFI_MAC_LEN];
      memcpy(client, payload + WIFI_MAC_LEN, WIFI_MAC_LEN);
      wifi_deauther_frame_type_t type = (wifi_deauther_frame_type_t)payload[12];
      return wifi_deauther_start_targeted(&target, client, type) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_APP_FLOOD: {
      if (len < WIFI_FLOOD_MIN_PAYLOAD)
        return SPI_STATUS_ERROR;
      uint8_t type = payload[0];
      uint8_t bssid[WIFI_MAC_LEN];
      memcpy(bssid, payload + 1, WIFI_MAC_LEN);
      uint8_t channel = (len >= 8) ? payload[7] : 1;
      if (type == WIFI_FLOOD_TYPE_AUTH)
        return wifi_flood_auth_start(bssid, channel) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
      if (type == WIFI_FLOOD_TYPE_ASSOC)
        return wifi_flood_assoc_start(bssid, channel) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
      if (type == WIFI_FLOOD_TYPE_PROBE)
        return wifi_flood_probe_start(bssid, channel) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
      return SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_APP_SNIFFER: {
      if (len < WIFI_SNIFFER_MIN_PAYLOAD)
        return SPI_STATUS_ERROR;
      spi_bridge_stream_enable(SPI_ID_WIFI_APP_SNIFFER, true);
      if (wifi_sniffer_start((wifi_sniffer_type_t)payload[0], payload[1])) {
        return SPI_STATUS_OK;
      }
      spi_bridge_stream_enable(SPI_ID_WIFI_APP_SNIFFER, false);
      return SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_APP_EVIL_TWIN:
      if (len == 0)
        return SPI_STATUS_INVALID_ARG;
      {
        char ssid[WIFI_SSID_BUF_LEN] = {0};
        uint8_t copy_len = (len > WIFI_SSID_MAX_LEN) ? WIFI_SSID_MAX_LEN : len;
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
        probe_monitor_record_t *results = probe_monitor_get_results(&count);
        spi_bridge_provide_results_dynamic(
            results, probe_monitor_get_count_ptr(), sizeof(probe_monitor_record_t));
        return SPI_STATUS_OK;
      }
      return SPI_STATUS_BUSY;

    case SPI_ID_WIFI_APP_SIGNAL_MON: {
      if (len < WIFI_SIGNAL_MON_MIN_LEN)
        return SPI_STATUS_ERROR;
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
      if (len < 2)
        return SPI_STATUS_INVALID_ARG;
      uint16_t snaplen = 0;
      memcpy(&snaplen, payload, sizeof(uint16_t));
      wifi_sniffer_set_snaplen(snaplen);
      return SPI_STATUS_OK;
    }

    case SPI_ID_WIFI_SNIFFER_SET_VERBOSE:
      if (len < 1)
        return SPI_STATUS_INVALID_ARG;
      wifi_sniffer_set_verbose(payload[0] != 0);
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_SNIFFER_SAVE_FLASH: {
      if (len < 1)
        return SPI_STATUS_INVALID_ARG;
      char filename[SPI_WIFI_SNIFFER_FILENAME_MAX] = {0};
      uint8_t copy_len =
          (len >= SPI_WIFI_SNIFFER_FILENAME_MAX) ? (SPI_WIFI_SNIFFER_FILENAME_MAX - 1) : len;
      memcpy(filename, payload, copy_len);
      return wifi_sniffer_save_to_internal_flash(filename) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_SNIFFER_SAVE_SD: {
      if (len < 1)
        return SPI_STATUS_INVALID_ARG;
      char filename[SPI_WIFI_SNIFFER_FILENAME_MAX] = {0};
      uint8_t copy_len =
          (len >= SPI_WIFI_SNIFFER_FILENAME_MAX) ? (SPI_WIFI_SNIFFER_FILENAME_MAX - 1) : len;
      memcpy(filename, payload, copy_len);
      return wifi_sniffer_save_to_sd_card(filename) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_SNIFFER_FREE_BUFFER:
      wifi_sniffer_free_buffer();
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_SNIFFER_STREAM_SD: {
      if (len < WIFI_SNIFFER_STREAM_MIN)
        return SPI_STATUS_INVALID_ARG;
      wifi_sniffer_type_t type = (wifi_sniffer_type_t)payload[0];
      uint8_t channel = payload[1];
      char filename[SPI_WIFI_SNIFFER_FILENAME_MAX] = {0};
      uint8_t name_len = (uint8_t)(len - 2);
      uint8_t copy_len = (name_len >= SPI_WIFI_SNIFFER_FILENAME_MAX)
                             ? (SPI_WIFI_SNIFFER_FILENAME_MAX - 1)
                             : name_len;
      memcpy(filename, payload + 2, copy_len);
      return wifi_sniffer_start_stream_sd(type, channel, filename) ? SPI_STATUS_OK
                                                                   : SPI_STATUS_ERROR;
    }

    case SPI_ID_WIFI_SNIFFER_CLEAR_PMKID:
      wifi_sniffer_clear_pmkid();
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_SNIFFER_GET_PMKID_BSSID:
      wifi_sniffer_get_pmkid_bssid(out_resp_payload);
      *out_resp_len = WIFI_MAC_LEN;
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_SNIFFER_CLEAR_HANDSHAKE:
      wifi_sniffer_clear_handshake();
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_SNIFFER_GET_HANDSHAKE_BSSID:
      wifi_sniffer_get_handshake_bssid(out_resp_payload);
      *out_resp_len = WIFI_MAC_LEN;
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_DEAUTH_STATUS:
      out_resp_payload[0] = wifi_deauther_is_running() ? 1 : 0;
      *out_resp_len = 1;
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_DEAUTH_SEND_RAW:
      if (len == 0)
        return SPI_STATUS_INVALID_ARG;
      wifi_deauther_send_raw_frame(payload, len);
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_ASSOC_REQUEST: {
      if (len < WIFI_ASSOC_MIN_PAYLOAD)
        return SPI_STATUS_INVALID_ARG;
      uint8_t ssid_len = payload[7];
      if (ssid_len > WIFI_SSID_MAX_LEN)
        return SPI_STATUS_INVALID_ARG;
      if (len < (uint8_t)(WIFI_ASSOC_MIN_PAYLOAD + ssid_len))
        return SPI_STATUS_INVALID_ARG;

      wifi_ap_record_t ap = {0};
      memcpy(ap.bssid, payload, WIFI_MAC_LEN);
      ap.primary = payload[6];
      memcpy(ap.ssid, payload + WIFI_ASSOC_MIN_PAYLOAD, ssid_len);
      ap.ssid[ssid_len] = '\0';

      wifi_deauther_send_association_request(&ap);
      return SPI_STATUS_OK;
    }

    case SPI_ID_WIFI_DEAUTH_SEND_FRAME: {
      if (len < WIFI_DEAUTH_FRAME_MIN)
        return SPI_STATUS_INVALID_ARG;
      wifi_ap_record_t ap = {0};
      memcpy(ap.bssid, payload, WIFI_MAC_LEN);
      ap.primary = payload[7];
      wifi_deauther_frame_type_t type = (wifi_deauther_frame_type_t)payload[6];
      wifi_deauther_send_deauth_frame(&ap, type);
      return SPI_STATUS_OK;
    }

    case SPI_ID_WIFI_DEAUTH_SEND_BROADCAST: {
      if (len < WIFI_DEAUTH_FRAME_MIN)
        return SPI_STATUS_INVALID_ARG;
      wifi_ap_record_t ap = {0};
      memcpy(ap.bssid, payload, WIFI_MAC_LEN);
      ap.primary = payload[7];
      wifi_deauther_frame_type_t type = (wifi_deauther_frame_type_t)payload[6];
      wifi_deauther_send_broadcast_deauth(&ap, type);
      return SPI_STATUS_OK;
    }

    case SPI_ID_WIFI_TARGET_SCAN_START: {
      if (len < WIFI_TARGET_MIN_PAYLOAD)
        return SPI_STATUS_INVALID_ARG;
      const uint8_t *bssid = payload;
      uint8_t channel = payload[6];
      if (target_scanner_start(bssid, channel)) {
        target_scanner_record_t *results = target_scanner_get_live_results(NULL, NULL);
        spi_bridge_provide_results_dynamic(
            results, target_scanner_get_count_ptr(), sizeof(target_scanner_record_t));
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
      if (len < WIFI_EVIL_TWIN_TMPL_MIN)
        return SPI_STATUS_INVALID_ARG;
      uint8_t ssid_len = payload[0];
      if (ssid_len > WIFI_SSID_MAX_LEN)
        return SPI_STATUS_INVALID_ARG;
      if (len < (uint8_t)(1 + ssid_len + 1))
        return SPI_STATUS_INVALID_ARG;
      uint8_t template_len = payload[1 + ssid_len];
      if (len < (uint8_t)(1 + ssid_len + 1 + template_len))
        return SPI_STATUS_INVALID_ARG;

      char ssid[WIFI_SSID_BUF_LEN] = {0};
      char template_path[WIFI_TEMPLATE_PATH_LEN] = {0};
      memcpy(ssid, payload + 1, ssid_len);
      if (template_len > 0) {
        uint8_t copy_len =
            (template_len >= sizeof(template_path)) ? (sizeof(template_path) - 1) : template_len;
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
      char password[WIFI_PASSWORD_MAX_LEN] = {0};
      evil_twin_get_last_password(password, sizeof(password));
      size_t out_len = strnlen(password, sizeof(password));
      if (out_len >= sizeof(password))
        out_len = sizeof(password) - 1;
      memcpy(out_resp_payload, password, out_len + 1);
      *out_resp_len = (uint8_t)(out_len + 1);
      return SPI_STATUS_OK;
    }

    case SPI_ID_WIFI_EVIL_TWIN_RESET_CAPTURE:
      evil_twin_reset_capture();
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_EVIL_TWIN_TMPL_BEGIN: {
      if (len < 2)
        return SPI_STATUS_ERROR;
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

// Static functions

static void promisc_noop_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
  (void)buf;
  (void)type;
}
