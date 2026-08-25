// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "wifi_dispatcher.h"

#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ap_scanner.h"
#include "beacon_spam.h"
#include "client_scanner.h"
#include "deauther_detector.h"
#include "session_manager.h"
#include "evil_twin.h"
#include "meshtastic_tcp.h"
#include "port_scan.h"
#include "probe_monitor.h"
#include "signal_monitor.h"
#include "spi_bridge.h"
#include "target_scanner.h"
#include "wifi_deauther.h"
#include "wifi_flood.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"

static const char *TAG = "WIFI_DISPATCHER";

// Compact scan results for the companion app (SPI_ID_WIFI_APP_SCAN_AP). Built
// from the raw scan once, then served through the generic data pipe.
static spi_wifi_scan_record_t s_app_scan_records[WIFI_SCAN_LIST_SIZE];
static spi_wifi_scan_record_ex_t s_app_scan_records_ex[WIFI_SCAN_LIST_SIZE];

// Port scan runs on the shared async runner so the SPI bridge (and the UI/app
// link it carries) never blocks for the seconds-to-minutes a sweep can take.
// The handler parses the request into s_port_scan_req and kicks the runner; the
// runner does the blocking scan, then publishes results via the generic data
// pipe. Callers poll SPI_ID_WIFI_SCAN_STATUS (the shared async-scan busy flag)
// and fetch results once it clears.
#define PORT_SCAN_MAX_RESULTS   32
#define PORT_SCAN_PORT_LIST_MAX 128 // fits a common-ports list within one SPI frame

typedef enum {
  PORT_SCAN_KIND_TARGET_RANGE,
  PORT_SCAN_KIND_TARGET_LIST,
  PORT_SCAN_KIND_NET_RANGE,
  PORT_SCAN_KIND_NET_LIST,
  PORT_SCAN_KIND_CIDR_RANGE,
  PORT_SCAN_KIND_CIDR_LIST,
} port_scan_kind_t;

typedef struct {
  port_scan_kind_t kind;
  char ip[16];
  char end_ip[16];
  uint8_t cidr;
  int start_port;
  int end_port;
  int ports[PORT_SCAN_PORT_LIST_MAX];
  int list_size;
  int max_results;
} port_scan_dispatch_req_t;

static port_scan_dispatch_req_t s_port_scan_req;
static port_scan_result_t s_port_scan_raw[PORT_SCAN_MAX_RESULTS];
static spi_port_scan_result_t s_port_scan_records[PORT_SCAN_MAX_RESULTS];
static uint16_t s_port_scan_count; // live count served by the dynamic data pipe

#define WIFI_SSID_MAX_LEN         32
#define WIFI_PASSWORD_MAX_LEN     64
#define WIFI_IP_ADDR_MAX_LEN      15
#define WIFI_DEAUTHER_MIN_PAYLOAD 13
#define WIFI_FLOOD_MIN_PAYLOAD    7
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

// Session kill callbacks: invoked when watchdog times out a session.
static void killed_deauther(spi_id_t id) {
  (void)id;
  wifi_deauther_stop();
}
static void killed_flood(spi_id_t id) {
  (void)id;
  wifi_flood_stop();
}
static void killed_evil_twin(spi_id_t id) {
  (void)id;
  evil_twin_stop_attack();
}
static void killed_beacon_spam(spi_id_t id) {
  (void)id;
  beacon_spam_stop();
}
static void killed_deauth_det(spi_id_t id) {
  (void)id;
  deauther_detector_stop();
}
static void killed_probe_mon(spi_id_t id) {
  (void)id;
  probe_monitor_stop();
}
static void killed_signal_mon(spi_id_t id) {
  (void)id;
  signal_monitor_stop();
}

// Helper: after a successful op start, open a session and write the response.
static spi_status_t open_session(spi_id_t op_id,
                                 session_kill_cb_t kill_cb,
                                 uint8_t *out_resp_payload,
                                 uint8_t *out_resp_len,
                                 void (*rollback_stop)(void)) {
  uint32_t sid = session_manager_start(op_id, kill_cb);
  if (sid == SPI_SESSION_INVALID_ID) {
    if (rollback_stop != NULL)
      rollback_stop();
    return SPI_STATUS_ERROR;
  }
  spi_session_resp_t resp = {.session_id = sid};
  memcpy(out_resp_payload, &resp, sizeof(resp));
  *out_resp_len = sizeof(resp);
  return SPI_STATUS_OK;
}

// Scan work functions run by the shared async runner (spi_bridge_async_scan_start).
// Each does the blocking scan and provides the results; the SPI handler returns
// immediately so the bridge stays free.
static void scan_fn_wifi_scan(void) {
  wifi_service_scan();
  spi_bridge_provide_results(
      wifi_service_get_ap_record(0), wifi_service_get_ap_count(), sizeof(wifi_ap_record_t));
}

static void scan_fn_app_ap(void) {
  wifi_service_scan();
  uint16_t count = wifi_service_get_ap_count();
  if (count > WIFI_SCAN_LIST_SIZE)
    count = WIFI_SCAN_LIST_SIZE;
  for (uint16_t i = 0; i < count; i++) {
    spi_wifi_scan_record_t *rec = &s_app_scan_records[i];
    memset(rec, 0, sizeof(*rec));
    const wifi_ap_record_t *ap = wifi_service_get_ap_record(i);
    if (ap == NULL)
      continue;
    memcpy(rec->bssid, ap->bssid, sizeof(rec->bssid));
    rec->rssi = ap->rssi;
    rec->channel = ap->primary;
    rec->authmode = (uint8_t)ap->authmode;
    size_t j = 0;
    for (; j < sizeof(rec->ssid) - 1 && ap->ssid[j] != '\0'; j++) {
      uint8_t c = ap->ssid[j];
      rec->ssid[j] = (c < 0x20 || c > 0x7E) ? '?' : c;
    }
    rec->ssid[j] = '\0';
  }
  spi_bridge_provide_results(s_app_scan_records, count, sizeof(spi_wifi_scan_record_t));
}

static void scan_fn_app_ap_detail(void) {
  wifi_service_scan();
  uint16_t count = wifi_service_get_ap_count();
  if (count > WIFI_SCAN_LIST_SIZE)
    count = WIFI_SCAN_LIST_SIZE;
  for (uint16_t i = 0; i < count; i++) {
    spi_wifi_scan_record_ex_t *rec = &s_app_scan_records_ex[i];
    memset(rec, 0, sizeof(*rec));
    const wifi_ap_record_t *ap = wifi_service_get_ap_record(i);
    if (ap == NULL)
      continue;
    memcpy(rec->bssid, ap->bssid, sizeof(rec->bssid));
    rec->rssi = ap->rssi;
    rec->channel = ap->primary;
    rec->second = (uint8_t)ap->second;
    rec->authmode = (uint8_t)ap->authmode;
    rec->pairwise_cipher = (uint8_t)ap->pairwise_cipher;
    rec->group_cipher = (uint8_t)ap->group_cipher;
    rec->phy =
        (uint8_t)((ap->phy_11b ? 0x01 : 0) | (ap->phy_11g ? 0x02 : 0) | (ap->phy_11n ? 0x04 : 0) |
                  (ap->phy_11ax ? 0x08 : 0) | (ap->phy_lr ? 0x10 : 0) | (ap->wps ? 0x20 : 0));
    memcpy(rec->country, ap->country.cc, sizeof(rec->country));
    size_t j = 0;
    for (; j < sizeof(rec->ssid) - 1 && ap->ssid[j] != '\0'; j++) {
      uint8_t c = ap->ssid[j];
      rec->ssid[j] = (c < 0x20 || c > 0x7E) ? '?' : c;
    }
    rec->ssid[j] = '\0';
  }
  spi_bridge_provide_results(s_app_scan_records_ex, count, sizeof(spi_wifi_scan_record_ex_t));
}

static void scan_fn_app_client(void) {
  if (!client_scanner_start())
    return;
  const TickType_t start = xTaskGetTickCount();
  const TickType_t timeout = pdMS_TO_TICKS(WIFI_CLIENT_SCAN_TIMEOUT);
  uint16_t count = 0;
  client_scanner_record_t *results = NULL;
  while ((results = client_scanner_get_results(&count)) == NULL) {
    if ((xTaskGetTickCount() - start) > timeout)
      return;
    vTaskDelay(pdMS_TO_TICKS(CLIENT_SCAN_POLL_DELAY_MS));
  }
  spi_bridge_provide_results(results, count, sizeof(client_scanner_record_t));
}

// Async runner body: run the scan captured in s_port_scan_req, convert the raw
// results to the wire record, and publish them through the data pipe.
// Live-publish each open port the instant it is found: convert the raw hit to
// the wire record and bump the count the data pipe exposes. A full sweep can run
// for minutes, so publishing only at the end would let the caller's poll time
// out and return nothing; with live publishing the P4 can fetch whatever has
// been found so far at any point.
static void port_scan_hit(const port_scan_result_t *hit, void *ctx) {
  (void)ctx;
  if (s_port_scan_count >= PORT_SCAN_MAX_RESULTS)
    return;
  spi_port_scan_result_t *dst = &s_port_scan_records[s_port_scan_count];
  memset(dst, 0, sizeof(*dst));
  strncpy(dst->ip_str, hit->ip_str, sizeof(dst->ip_str) - 1);
  dst->port = (uint16_t)hit->port;
  dst->protocol = (uint8_t)hit->protocol;
  dst->status = (uint8_t)hit->status;
  strncpy(dst->banner, hit->banner, sizeof(dst->banner) - 1);
  s_port_scan_count++; // bump last: the P4 only reads indices below the count
}

static void scan_fn_port_scan(void) {
  const port_scan_dispatch_req_t *r = &s_port_scan_req;
  switch (r->kind) {
    case PORT_SCAN_KIND_TARGET_RANGE:
      port_scan_target_range(r->ip, r->start_port, r->end_port, s_port_scan_raw, r->max_results);
      break;
    case PORT_SCAN_KIND_TARGET_LIST:
      port_scan_target_list(r->ip, r->ports, r->list_size, s_port_scan_raw, r->max_results);
      break;
    case PORT_SCAN_KIND_NET_RANGE:
      port_scan_network_range_using_port_range(
          r->ip, r->end_ip, r->start_port, r->end_port, s_port_scan_raw, r->max_results);
      break;
    case PORT_SCAN_KIND_NET_LIST:
      port_scan_network_range_using_port_list(
          r->ip, r->end_ip, r->ports, r->list_size, s_port_scan_raw, r->max_results);
      break;
    case PORT_SCAN_KIND_CIDR_RANGE:
      port_scan_cidr_using_port_range(
          r->ip, r->cidr, r->start_port, r->end_port, s_port_scan_raw, r->max_results);
      break;
    case PORT_SCAN_KIND_CIDR_LIST:
      port_scan_cidr_using_port_list(
          r->ip, r->cidr, r->ports, r->list_size, s_port_scan_raw, r->max_results);
      break;
  }
  // Results were published live via port_scan_hit; nothing to do at the end.
}

// Kick the port scan off on the shared async runner. Rejects if a scan (AP,
// client, or port) is already running. Publishes results through the dynamic
// data pipe so the P4 can read partial results while the sweep is still running.
static spi_status_t start_port_scan(void) {
  if (spi_bridge_async_scan_busy())
    return SPI_STATUS_BUSY;
  s_port_scan_count = 0;
  port_scan_reset_abort();
  port_scan_set_hit_cb(port_scan_hit, NULL);
  spi_bridge_provide_results_dynamic(
      s_port_scan_records, &s_port_scan_count, sizeof(spi_port_scan_result_t));
  return spi_bridge_async_scan_start(scan_fn_port_scan) ? SPI_STATUS_OK : SPI_STATUS_BUSY;
}

// Parse a packed port list: [max_results u16][count u16][ports u16 * count].
// Returns false if the payload is malformed. Clamps count to the static cap.
static bool parse_port_list(const uint8_t *p, uint8_t len, size_t offset) {
  if (len < offset + 4)
    return false;
  uint16_t max_res = (uint16_t)(p[offset] | (p[offset + 1] << 8));
  uint16_t count = (uint16_t)(p[offset + 2] | (p[offset + 3] << 8));
  offset += 4;
  if (count > PORT_SCAN_PORT_LIST_MAX)
    count = PORT_SCAN_PORT_LIST_MAX;
  if (len < offset + (size_t)count * 2)
    return false;
  for (uint16_t i = 0; i < count; i++) {
    s_port_scan_req.ports[i] = (uint16_t)(p[offset] | (p[offset + 1] << 8));
    offset += 2;
  }
  s_port_scan_req.list_size = count;
  s_port_scan_req.max_results =
      (max_res == 0 || max_res > PORT_SCAN_MAX_RESULTS) ? PORT_SCAN_MAX_RESULTS : max_res;
  return true;
}

static int clamp_max_results(uint16_t requested) {
  if (requested == 0 || requested > PORT_SCAN_MAX_RESULTS)
    return PORT_SCAN_MAX_RESULTS;
  return requested;
}

spi_status_t wifi_dispatcher_execute(spi_id_t id,
                                     const uint8_t *payload,
                                     uint8_t len,
                                     uint8_t *out_resp_payload,
                                     uint8_t *out_resp_len) {
  (void)TAG;
  *out_resp_len = 0;

  switch (id) {
    case SPI_ID_WIFI_SCAN:
      return spi_bridge_async_scan_start(scan_fn_wifi_scan) ? SPI_STATUS_OK : SPI_STATUS_BUSY;

    case SPI_ID_WIFI_SCAN_STATUS:
      out_resp_payload[0] = spi_bridge_async_scan_busy() ? 1 : 0;
      *out_resp_len = 1;
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
      return spi_bridge_async_scan_start(scan_fn_app_ap) ? SPI_STATUS_OK : SPI_STATUS_BUSY;

    case SPI_ID_WIFI_APP_SCAN_AP_DETAIL:
      return spi_bridge_async_scan_start(scan_fn_app_ap_detail) ? SPI_STATUS_OK : SPI_STATUS_BUSY;

    case SPI_ID_WIFI_APP_SCAN_CLIENT:
      return spi_bridge_async_scan_start(scan_fn_app_client) ? SPI_STATUS_OK : SPI_STATUS_BUSY;

    case SPI_ID_WIFI_APP_BEACON_SPAM: {
      bool ok;
      if (len == 0) {
        ok = beacon_spam_start_random();
      } else {
        char path[WIFI_PATH_MAX_LEN] = {0};
        size_t copy_len = len;
        if (copy_len >= sizeof(path))
          copy_len = sizeof(path) - 1;
        memcpy(path, payload, copy_len);
        ok = beacon_spam_start_custom(path);
      }
      if (!ok)
        return SPI_STATUS_ERROR;
      return open_session(SPI_ID_WIFI_APP_BEACON_SPAM,
                          killed_beacon_spam,
                          out_resp_payload,
                          out_resp_len,
                          beacon_spam_stop);
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
      if (!wifi_deauther_start_targeted(&target, client, type))
        return SPI_STATUS_ERROR;
      return open_session(SPI_ID_WIFI_APP_DEAUTHER,
                          killed_deauther,
                          out_resp_payload,
                          out_resp_len,
                          wifi_deauther_stop);
    }

    case SPI_ID_WIFI_APP_FLOOD: {
      if (len < WIFI_FLOOD_MIN_PAYLOAD)
        return SPI_STATUS_ERROR;
      uint8_t type = payload[0];
      uint8_t bssid[WIFI_MAC_LEN];
      memcpy(bssid, payload + 1, WIFI_MAC_LEN);
      uint8_t channel = (len >= 8) ? payload[7] : 1;
      bool ok = false;
      if (type == WIFI_FLOOD_TYPE_AUTH)
        ok = wifi_flood_auth_start(bssid, channel);
      else if (type == WIFI_FLOOD_TYPE_ASSOC)
        ok = wifi_flood_assoc_start(bssid, channel);
      else if (type == WIFI_FLOOD_TYPE_PROBE)
        ok = wifi_flood_probe_start(bssid, channel);
      if (!ok)
        return SPI_STATUS_ERROR;
      return open_session(
          SPI_ID_WIFI_APP_FLOOD, killed_flood, out_resp_payload, out_resp_len, wifi_flood_stop);
    }

    case SPI_ID_WIFI_APP_SNIFFER: {
      // The companion app's live view wants raw frames across every channel. If
      // it omits the args, default to RAW + channel 0 (hopping) instead of
      // rejecting, so an empty START still streams something useful.
      // payload[0]: sniffer type, payload[1]: channel (0 = hop all).
      // payload[2] (optional): monitor_mode flag — when set, buffer recycles
      // on overflow and packet counter keeps growing (used by Packet Monitor).
      wifi_sniffer_type_t type =
          (len >= 1) ? (wifi_sniffer_type_t)payload[0] : WIFI_SNIFFER_TYPE_RAW;
      uint8_t channel = (len >= 2) ? payload[1] : 0;
      bool monitor_mode = (len >= 3) && (payload[2] != 0);
      wifi_sniffer_set_monitor_mode(monitor_mode);
      spi_bridge_stream_enable(SPI_ID_WIFI_APP_SNIFFER, true);
      if (!wifi_sniffer_start(type, channel)) {
        spi_bridge_stream_enable(SPI_ID_WIFI_APP_SNIFFER, false);
        return SPI_STATUS_ERROR;
      }
      uint32_t sid = session_manager_start(SPI_ID_WIFI_APP_SNIFFER, wifi_sniffer_session_killed);
      if (sid == SPI_SESSION_INVALID_ID) {
        wifi_sniffer_stop();
        spi_bridge_stream_enable(SPI_ID_WIFI_APP_SNIFFER, false);
        return SPI_STATUS_ERROR;
      }
      wifi_sniffer_bind_session(sid);
      spi_session_resp_t resp = {.session_id = sid};
      memcpy(out_resp_payload, &resp, sizeof(resp));
      *out_resp_len = sizeof(resp);
      return SPI_STATUS_OK;
    }

    case SPI_ID_WIFI_APP_EVIL_TWIN: {
      if (len == 0)
        return SPI_STATUS_INVALID_ARG;
      char ssid[WIFI_SSID_BUF_LEN] = {0};
      uint8_t copy_len = (len > WIFI_SSID_MAX_LEN) ? WIFI_SSID_MAX_LEN : len;
      memcpy(ssid, payload, copy_len);
      // Async: the portal bring-up (~1.5 s) runs on a task so this returns the
      // session id immediately and the P4 can start heartbeating right away.
      evil_twin_start_attack_async(ssid, NULL);
      return open_session(SPI_ID_WIFI_APP_EVIL_TWIN,
                          killed_evil_twin,
                          out_resp_payload,
                          out_resp_len,
                          evil_twin_stop_attack);
    }

    case SPI_ID_WIFI_APP_DEAUTH_DET:
      deauther_detector_start();
      return open_session(SPI_ID_WIFI_APP_DEAUTH_DET,
                          killed_deauth_det,
                          out_resp_payload,
                          out_resp_len,
                          deauther_detector_stop);

    case SPI_ID_WIFI_APP_PROBE_MON:
      if (!probe_monitor_start())
        return SPI_STATUS_BUSY;
      {
        uint16_t count;
        probe_monitor_record_t *results = probe_monitor_get_results(&count);
        spi_bridge_provide_results_dynamic(
            results, probe_monitor_get_count_ptr(), sizeof(probe_monitor_record_t));
      }
      return open_session(SPI_ID_WIFI_APP_PROBE_MON,
                          killed_probe_mon,
                          out_resp_payload,
                          out_resp_len,
                          probe_monitor_stop);

    case SPI_ID_WIFI_APP_SIGNAL_MON: {
      if (len < WIFI_SIGNAL_MON_MIN_LEN)
        return SPI_STATUS_ERROR;
      signal_monitor_start(payload, payload[6]);
      return open_session(SPI_ID_WIFI_APP_SIGNAL_MON,
                          killed_signal_mon,
                          out_resp_payload,
                          out_resp_len,
                          signal_monitor_stop);
    }

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
      evil_twin_start_attack_async(ssid, template_path);
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

    case SPI_ID_WIFI_PORT_SCAN_TARGET_RANGE: {
      if (len < sizeof(spi_port_scan_range_req_t))
        return SPI_STATUS_INVALID_ARG;
      const spi_port_scan_range_req_t *req = (const spi_port_scan_range_req_t *)payload;
      memset(&s_port_scan_req, 0, sizeof(s_port_scan_req));
      s_port_scan_req.kind = PORT_SCAN_KIND_TARGET_RANGE;
      memcpy(s_port_scan_req.ip, req->ip, sizeof(s_port_scan_req.ip));
      s_port_scan_req.ip[sizeof(s_port_scan_req.ip) - 1] = '\0';
      s_port_scan_req.start_port = req->start_port;
      s_port_scan_req.end_port = req->end_port;
      s_port_scan_req.max_results = clamp_max_results(req->max_results);
      return start_port_scan();
    }

    case SPI_ID_WIFI_PORT_SCAN_TARGET_LIST: {
      // Packed: ip[16][max_results u16][count u16][ports u16 * count].
      if (len < 16)
        return SPI_STATUS_INVALID_ARG;
      memset(&s_port_scan_req, 0, sizeof(s_port_scan_req));
      s_port_scan_req.kind = PORT_SCAN_KIND_TARGET_LIST;
      memcpy(s_port_scan_req.ip, payload, sizeof(s_port_scan_req.ip));
      s_port_scan_req.ip[sizeof(s_port_scan_req.ip) - 1] = '\0';
      if (!parse_port_list(payload, len, 16))
        return SPI_STATUS_INVALID_ARG;
      return start_port_scan();
    }

    case SPI_ID_WIFI_PORT_SCAN_NETWORK: {
      // Range and list share this id; a payload the exact size of the range
      // struct is the range variant, anything else is the packed list variant.
      memset(&s_port_scan_req, 0, sizeof(s_port_scan_req));
      if (len == sizeof(spi_port_scan_network_req_t)) {
        const spi_port_scan_network_req_t *req = (const spi_port_scan_network_req_t *)payload;
        s_port_scan_req.kind = PORT_SCAN_KIND_NET_RANGE;
        memcpy(s_port_scan_req.ip, req->start_ip, sizeof(s_port_scan_req.ip));
        s_port_scan_req.ip[sizeof(s_port_scan_req.ip) - 1] = '\0';
        memcpy(s_port_scan_req.end_ip, req->end_ip, sizeof(s_port_scan_req.end_ip));
        s_port_scan_req.end_ip[sizeof(s_port_scan_req.end_ip) - 1] = '\0';
        s_port_scan_req.start_port = req->start_port;
        s_port_scan_req.end_port = req->end_port;
        s_port_scan_req.max_results = clamp_max_results(req->max_results);
      } else {
        // Packed list: start_ip[16] end_ip[16] [max u16][count u16][ports...].
        if (len < 32)
          return SPI_STATUS_INVALID_ARG;
        s_port_scan_req.kind = PORT_SCAN_KIND_NET_LIST;
        memcpy(s_port_scan_req.ip, payload, sizeof(s_port_scan_req.ip));
        s_port_scan_req.ip[sizeof(s_port_scan_req.ip) - 1] = '\0';
        memcpy(s_port_scan_req.end_ip, payload + 16, sizeof(s_port_scan_req.end_ip));
        s_port_scan_req.end_ip[sizeof(s_port_scan_req.end_ip) - 1] = '\0';
        if (!parse_port_list(payload, len, 32))
          return SPI_STATUS_INVALID_ARG;
      }
      return start_port_scan();
    }

    case SPI_ID_WIFI_PORT_SCAN_CIDR: {
      // Range and list share this id; disambiguated by payload size as above.
      memset(&s_port_scan_req, 0, sizeof(s_port_scan_req));
      if (len == sizeof(spi_port_scan_cidr_req_t)) {
        const spi_port_scan_cidr_req_t *req = (const spi_port_scan_cidr_req_t *)payload;
        s_port_scan_req.kind = PORT_SCAN_KIND_CIDR_RANGE;
        memcpy(s_port_scan_req.ip, req->base_ip, sizeof(s_port_scan_req.ip));
        s_port_scan_req.ip[sizeof(s_port_scan_req.ip) - 1] = '\0';
        s_port_scan_req.cidr = req->cidr;
        s_port_scan_req.start_port = req->start_port;
        s_port_scan_req.end_port = req->end_port;
        s_port_scan_req.max_results = clamp_max_results(req->max_results);
      } else {
        // Packed list: base_ip[16][cidr u8][max u16][count u16][ports...].
        if (len < 17)
          return SPI_STATUS_INVALID_ARG;
        s_port_scan_req.kind = PORT_SCAN_KIND_CIDR_LIST;
        memcpy(s_port_scan_req.ip, payload, sizeof(s_port_scan_req.ip));
        s_port_scan_req.ip[sizeof(s_port_scan_req.ip) - 1] = '\0';
        s_port_scan_req.cidr = payload[16];
        if (!parse_port_list(payload, len, 17))
          return SPI_STATUS_INVALID_ARG;
      }
      return start_port_scan();
    }

    case SPI_ID_WIFI_PORT_SCAN_STOP:
      port_scan_request_abort();
      return SPI_STATUS_OK;

    case SPI_ID_WIFI_GET_MAC: {
      uint8_t iface = (len >= 1) ? payload[0] : 0;
      wifi_interface_t wif = (iface == 1) ? WIFI_IF_AP : WIFI_IF_STA;
      uint8_t mac[WIFI_MAC_LEN] = {0};
      if (esp_wifi_get_mac(wif, mac) != ESP_OK)
        return SPI_STATUS_ERROR;
      memcpy(out_resp_payload, mac, sizeof(mac));
      *out_resp_len = sizeof(mac);
      return SPI_STATUS_OK;
    }

    case SPI_ID_WIFI_GET_IP_INFO: {
      uint8_t iface = (len >= 1) ? payload[0] : 0;
      const char *key = (iface == 1) ? "WIFI_AP_DEF" : "WIFI_STA_DEF";
      esp_netif_t *netif = esp_netif_get_handle_from_ifkey(key);
      if (netif == NULL)
        return SPI_STATUS_ERROR;
      esp_netif_ip_info_t ip = {0};
      esp_netif_get_ip_info(netif, &ip);
      spi_wifi_ip_info_t info = {0};
      info.interface = iface;
      esp_wifi_get_mac((iface == 1) ? WIFI_IF_AP : WIFI_IF_STA, info.mac);
      info.ip = ip.ip.addr;
      info.netmask = ip.netmask.addr;
      info.gw = ip.gw.addr;
      memcpy(out_resp_payload, &info, sizeof(info));
      *out_resp_len = sizeof(info);
      return SPI_STATUS_OK;
    }

    case SPI_ID_MESH_WIFI_INIT: {
      if (len < sizeof(spi_mesh_init_t)) {
        return SPI_STATUS_INVALID_ARG;
      }
      spi_mesh_init_t req;
      memcpy(&req, payload, sizeof(req));
      return (meshtastic_tcp_init(req.node_num) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_MESH_WIFI_STOP:
      meshtastic_tcp_stop();
      return SPI_STATUS_OK;

    default:
      return SPI_STATUS_ERROR;
  }
}

// Static functions

static void promisc_noop_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
  (void)buf;
  (void)type;
}
