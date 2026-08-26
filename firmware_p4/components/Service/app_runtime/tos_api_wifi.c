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

// WiFi subsystem of the app ABI (`api->wifi->...`). Curated typed wrappers over
// the P4 wifi service and the SPI bridge to the C5 radio; each gated by a
// capability. This is the reference for exposing a firmware subsystem to apps:
// the app never sees the SPI ids or payload layouts, only typed functions.

#include "tos_api.h"

#include <string.h>

#include "spi_bridge.h"
#include "spi_protocol.h"
#include "tos_app_ctx.h"
#include "wifi_service.h"

#define NEED(cap)                        \
  do {                                   \
    if (!tos_app_cap_check(cap))         \
      return ESP_ERR_NOT_SUPPORTED;      \
  } while (0)

// Send one command to the C5 and return its session id (first 4 bytes of the
// response), if any.
static esp_err_t send_session(spi_id_t id, const uint8_t *pl, uint8_t len, uint32_t *out_session) {
  spi_header_t hdr = {0};
  uint8_t resp[8] = {0};
  esp_err_t e =
      spi_bridge_send_command(id, pl, len, &hdr, resp, sizeof(resp), spi_bridge_get_timeout(id));
  if (e != ESP_OK)
    return e;
  if (out_session != NULL && hdr.length >= 4)
    memcpy(out_session, resp, 4);
  return ESP_OK;
}

static esp_err_t send_void(spi_id_t id, const uint8_t *pl, uint8_t len) {
  spi_header_t hdr = {0};
  uint8_t resp[8] = {0};
  return spi_bridge_send_command(id, pl, len, &hdr, resp, sizeof(resp), spi_bridge_get_timeout(id));
}

// --- discovery / station ---------------------------------------------------

static esp_err_t w_scan(void) {
  NEED(TOS_CAP_RADIO_RX);
  return wifi_service_scan();
}

static int w_ap_count(void) {
  return tos_app_cap_check(TOS_CAP_RADIO_RX) ? (int)wifi_service_get_ap_count() : 0;
}

static esp_err_t w_ap_get(int index, tos_wifi_ap_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  if (out == NULL || index < 0)
    return ESP_ERR_INVALID_ARG;
  wifi_ap_record_t *r = wifi_service_get_ap_record((uint16_t)index);
  if (r == NULL)
    return ESP_ERR_NOT_FOUND;
  memset(out, 0, sizeof(*out));
  strlcpy(out->ssid, (const char *)r->ssid, sizeof(out->ssid));
  memcpy(out->bssid, r->bssid, 6);
  out->rssi = r->rssi;
  out->channel = r->primary;
  out->authmode = (uint8_t)r->authmode;
  return ESP_OK;
}

static bool w_is_connected(void) {
  return wifi_service_is_connected();
}

static const char *w_connected_ssid(void) {
  return wifi_service_get_connected_ssid();
}

static esp_err_t w_connect(const char *ssid, const char *password) {
  NEED(TOS_CAP_RADIO_TX);
  return wifi_service_connect_to_ap(ssid, password);
}

static esp_err_t w_disconnect(void) {
  NEED(TOS_CAP_RADIO_TX);
  return send_void(SPI_ID_WIFI_DISCONNECT, NULL, 0);
}

// --- monitors (radio-rx) ---------------------------------------------------

static esp_err_t w_sniffer_start(uint8_t type, uint8_t channel, bool monitor, uint32_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  uint8_t pl[3] = {type, channel, monitor ? 1 : 0};
  return send_session(SPI_ID_WIFI_APP_SNIFFER, pl, sizeof(pl), out);
}

static esp_err_t w_deauth_detect(uint32_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  return send_session(SPI_ID_WIFI_APP_DEAUTH_DET, NULL, 0, out);
}

static esp_err_t w_probe_monitor(uint32_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  return send_session(SPI_ID_WIFI_APP_PROBE_MON, NULL, 0, out);
}

static esp_err_t w_signal_monitor(const uint8_t bssid[6], uint8_t channel, uint32_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  uint8_t pl[7];
  memcpy(pl, bssid, 6);
  pl[6] = channel;
  return send_session(SPI_ID_WIFI_APP_SIGNAL_MON, pl, sizeof(pl), out);
}

// --- offensive (radio-tx) --------------------------------------------------

static esp_err_t
w_deauth(const uint8_t bssid[6], const uint8_t client[6], uint8_t type, uint8_t channel, uint32_t *out) {
  NEED(TOS_CAP_RADIO_TX);
  uint8_t pl[14];
  memcpy(pl, bssid, 6);
  memcpy(pl + 6, client, 6);
  pl[12] = type;
  pl[13] = channel;
  return send_session(SPI_ID_WIFI_APP_DEAUTHER, pl, sizeof(pl), out);
}

static esp_err_t w_deauth_broadcast(const uint8_t bssid[6], uint8_t type, uint8_t channel) {
  NEED(TOS_CAP_RADIO_TX);
  uint8_t pl[8];
  memcpy(pl, bssid, 6);
  pl[6] = type;
  pl[7] = channel;
  return send_void(SPI_ID_WIFI_DEAUTH_SEND_BROADCAST, pl, sizeof(pl));
}

static esp_err_t w_flood(uint8_t type, const uint8_t bssid[6], uint8_t channel, uint32_t *out) {
  NEED(TOS_CAP_RADIO_TX);
  uint8_t pl[8];
  pl[0] = type;
  memcpy(pl + 1, bssid, 6);
  pl[7] = channel;
  return send_session(SPI_ID_WIFI_APP_FLOOD, pl, sizeof(pl), out);
}

static esp_err_t w_beacon_spam(const char *ssid_list_path, uint32_t *out) {
  NEED(TOS_CAP_RADIO_TX);
  const uint8_t *pl = (const uint8_t *)ssid_list_path;
  uint8_t len = ssid_list_path ? (uint8_t)strnlen(ssid_list_path, 200) : 0;
  return send_session(SPI_ID_WIFI_APP_BEACON_SPAM, pl, len, out);
}

static esp_err_t w_evil_twin(const char *ssid, uint32_t *out) {
  NEED(TOS_CAP_RADIO_TX);
  if (ssid == NULL || ssid[0] == '\0')
    return ESP_ERR_INVALID_ARG;
  return send_session(SPI_ID_WIFI_APP_EVIL_TWIN, (const uint8_t *)ssid,
                      (uint8_t)strnlen(ssid, 32), out);
}

// --- session / radio control -----------------------------------------------

static esp_err_t w_session_stop(uint32_t session) {
  uint8_t pl[4];
  memcpy(pl, &session, 4);
  return send_void(SPI_ID_SESSION_STOP, pl, sizeof(pl));
}

static esp_err_t w_channel_hop(bool enable) {
  NEED(TOS_CAP_RADIO_TX);
  return send_void(enable ? SPI_ID_WIFI_CH_HOP_START : SPI_ID_WIFI_CH_HOP_STOP, NULL, 0);
}

const tos_wifi_api_t tos_wifi_api_impl = {
    .scan = w_scan,
    .ap_count = w_ap_count,
    .ap_get = w_ap_get,
    .is_connected = w_is_connected,
    .connected_ssid = w_connected_ssid,
    .connect = w_connect,
    .disconnect = w_disconnect,
    .sniffer_start = w_sniffer_start,
    .deauth_detect = w_deauth_detect,
    .probe_monitor = w_probe_monitor,
    .signal_monitor = w_signal_monitor,
    .deauth = w_deauth,
    .deauth_broadcast = w_deauth_broadcast,
    .flood = w_flood,
    .beacon_spam = w_beacon_spam,
    .evil_twin = w_evil_twin,
    .session_stop = w_session_stop,
    .channel_hop = w_channel_hop,
};
