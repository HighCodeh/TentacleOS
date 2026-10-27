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
//
// Session ops hold the shared WiFi radio through resource_mgr (RES_WIFI) for the
// life of the session, so an app cannot run while the on-device UI holds it, two
// apps cannot fight over it, and a dead app's radio is reclaimed on teardown.

#include "tos_api.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "resource_mgr.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "tos_app_ctx.h"
#include "wifi_service.h"

#define NEED(cap)                   \
  do {                              \
    if (!tos_app_cap_check(cap))    \
      return ESP_ERR_NOT_SUPPORTED; \
  } while (0)

// One WiFi radio session an app started, tied to its RES_WIFI grant so a
// preemption or the app's teardown can stop the C5 session and free the radio.
#define WIFI_APP_SESSIONS 4

typedef struct {
  bool active;
  uint32_t session; // C5 session id, SPI_SESSION_INVALID_ID until the start returns
  res_handle_t handle;
  void *task;
} wifi_grant_t;

static wifi_grant_t s_wgrants[WIFI_APP_SESSIONS];
static SemaphoreHandle_t s_wgrants_lock = NULL;

static bool ensure_wgrants_lock(void) {
  if (s_wgrants_lock == NULL)
    s_wgrants_lock = xSemaphoreCreateMutex();
  return s_wgrants_lock != NULL;
}

static void clear_record(wifi_grant_t *g) {
  g->active = false;
  g->session = SPI_SESSION_INVALID_ID;
  g->handle = RES_HANDLE_NONE;
  g->task = NULL;
}

// resource_mgr fires this (outside its lock) when this app's RES_WIFI grant is
// preempted or bulk-released on teardown. The grant is already gone; stop the
// C5 session so the radio is actually freed, then drop the record.
static void wifi_grant_revoked(void *user) {
  wifi_grant_t *g = (wifi_grant_t *)user;
  uint32_t session = SPI_SESSION_INVALID_ID;
  void *task = NULL;
  xSemaphoreTake(s_wgrants_lock, portMAX_DELAY);
  if (g->active) {
    session = g->session;
    task = g->task;
  }
  clear_record(g);
  xSemaphoreGive(s_wgrants_lock);
  tos_app_ctx_signal_resource_lost(task);
  if (session != SPI_SESSION_INVALID_ID) {
    spi_header_t hdr = {0};
    uint8_t resp[8] = {0};
    uint8_t pl[4];
    memcpy(pl, &session, 4);
    spi_bridge_send_command(SPI_ID_SESSION_STOP, pl, sizeof(pl), &hdr, resp, sizeof(resp),
                            spi_bridge_get_timeout(SPI_ID_SESSION_STOP));
  }
}

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

// Reserve RES_WIFI for this app, start the C5 session, and record the pair.
// Returns ESP_ERR_INVALID_STATE (BUSY) if the radio is held by the UI or another
// app, so the calling app can back off.
static esp_err_t start_wifi_session(spi_id_t id, const uint8_t *pl, uint8_t len,
                                    uint32_t *out_session) {
  if (out_session != NULL)
    *out_session = SPI_SESSION_INVALID_ID;
  if (!ensure_wgrants_lock())
    return ESP_ERR_NO_MEM;

  xSemaphoreTake(s_wgrants_lock, portMAX_DELAY);
  wifi_grant_t *g = NULL;
  for (int i = 0; i < WIFI_APP_SESSIONS; i++) {
    if (!s_wgrants[i].active) {
      g = &s_wgrants[i];
      clear_record(g);
      g->active = true;
      g->task = xTaskGetCurrentTaskHandle();
      break;
    }
  }
  xSemaphoreGive(s_wgrants_lock);
  if (g == NULL)
    return ESP_ERR_NO_MEM;

  res_request_t req = {.id = RES_WIFI,
                       .lane = RES_LANE_MAIN,
                       .owner_kind = RES_OWNER_APP,
                       .owner_task = g->task,
                       .allow_preempt = false, // apps never preempt the UI or each other
                       .on_revoke = wifi_grant_revoked,
                       .user = g};
  res_handle_t handle = RES_HANDLE_NONE;
  if (resource_acquire(&req, &handle) != ESP_OK) {
    xSemaphoreTake(s_wgrants_lock, portMAX_DELAY);
    clear_record(g);
    xSemaphoreGive(s_wgrants_lock);
    return ESP_ERR_INVALID_STATE; // radio busy
  }
  g->handle = handle;

  uint32_t session = SPI_SESSION_INVALID_ID;
  esp_err_t e = send_session(id, pl, len, &session);
  if (e != ESP_OK || session == SPI_SESSION_INVALID_ID) {
    resource_release(handle);
    xSemaphoreTake(s_wgrants_lock, portMAX_DELAY);
    clear_record(g);
    xSemaphoreGive(s_wgrants_lock);
    return (e != ESP_OK) ? e : ESP_FAIL;
  }

  // If we were preempted while the start was in flight, stop the orphan session.
  xSemaphoreTake(s_wgrants_lock, portMAX_DELAY);
  bool still_ours = g->active;
  if (still_ours)
    g->session = session;
  xSemaphoreGive(s_wgrants_lock);
  if (!still_ours) {
    uint8_t pl_stop[4];
    memcpy(pl_stop, &session, 4);
    send_void(SPI_ID_SESSION_STOP, pl_stop, sizeof(pl_stop));
    return ESP_ERR_INVALID_STATE;
  }

  tos_app_ctx_clear_resource_lost(); // fresh session: forget any earlier preemption
  if (out_session != NULL)
    *out_session = session;
  return ESP_OK;
}

// One-shot radio transmit: refuse if the radio is held, otherwise send and free.
static esp_err_t send_momentary(spi_id_t id, const uint8_t *pl, uint8_t len) {
  res_request_t req = {.id = RES_WIFI,
                       .lane = RES_LANE_MAIN,
                       .owner_kind = RES_OWNER_APP,
                       .owner_task = xTaskGetCurrentTaskHandle(),
                       .allow_preempt = false};
  res_handle_t handle = RES_HANDLE_NONE;
  if (resource_acquire(&req, &handle) != ESP_OK)
    return ESP_ERR_INVALID_STATE;
  esp_err_t e = send_void(id, pl, len);
  resource_release(handle);
  return e;
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
  return start_wifi_session(SPI_ID_WIFI_APP_SNIFFER, pl, sizeof(pl), out);
}

static esp_err_t w_deauth_detect(uint32_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  return start_wifi_session(SPI_ID_WIFI_APP_DEAUTH_DET, NULL, 0, out);
}

static esp_err_t w_probe_monitor(uint32_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  return start_wifi_session(SPI_ID_WIFI_APP_PROBE_MON, NULL, 0, out);
}

static esp_err_t w_signal_monitor(const uint8_t bssid[6], uint8_t channel, uint32_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  uint8_t pl[7];
  memcpy(pl, bssid, 6);
  pl[6] = channel;
  return start_wifi_session(SPI_ID_WIFI_APP_SIGNAL_MON, pl, sizeof(pl), out);
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
  return start_wifi_session(SPI_ID_WIFI_APP_DEAUTHER, pl, sizeof(pl), out);
}

static esp_err_t w_deauth_broadcast(const uint8_t bssid[6], uint8_t type, uint8_t channel) {
  NEED(TOS_CAP_RADIO_TX);
  uint8_t pl[8];
  memcpy(pl, bssid, 6);
  pl[6] = type;
  pl[7] = channel;
  return send_momentary(SPI_ID_WIFI_DEAUTH_SEND_BROADCAST, pl, sizeof(pl));
}

static esp_err_t w_flood(uint8_t type, const uint8_t bssid[6], uint8_t channel, uint32_t *out) {
  NEED(TOS_CAP_RADIO_TX);
  uint8_t pl[8];
  pl[0] = type;
  memcpy(pl + 1, bssid, 6);
  pl[7] = channel;
  return start_wifi_session(SPI_ID_WIFI_APP_FLOOD, pl, sizeof(pl), out);
}

static esp_err_t w_beacon_spam(const char *ssid_list_path, uint32_t *out) {
  NEED(TOS_CAP_RADIO_TX);
  const uint8_t *pl = (const uint8_t *)ssid_list_path;
  uint8_t len = ssid_list_path ? (uint8_t)strnlen(ssid_list_path, 200) : 0;
  return start_wifi_session(SPI_ID_WIFI_APP_BEACON_SPAM, pl, len, out);
}

static esp_err_t w_evil_twin(const char *ssid, uint32_t *out) {
  NEED(TOS_CAP_RADIO_TX);
  if (ssid == NULL || ssid[0] == '\0')
    return ESP_ERR_INVALID_ARG;
  return start_wifi_session(SPI_ID_WIFI_APP_EVIL_TWIN, (const uint8_t *)ssid,
                            (uint8_t)strnlen(ssid, 32), out);
}

// --- session / radio control -----------------------------------------------

static esp_err_t w_session_stop(uint32_t session) {
  res_handle_t handle = RES_HANDLE_NONE;
  if (ensure_wgrants_lock()) {
    xSemaphoreTake(s_wgrants_lock, portMAX_DELAY);
    for (int i = 0; i < WIFI_APP_SESSIONS; i++) {
      if (s_wgrants[i].active && s_wgrants[i].session == session) {
        handle = s_wgrants[i].handle;
        clear_record(&s_wgrants[i]);
        break;
      }
    }
    xSemaphoreGive(s_wgrants_lock);
  }
  resource_release(handle); // no-op if not ours / already gone
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
