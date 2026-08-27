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

// BLE subsystem of the app ABI (`api->ble->...`). Curated typed wrappers over the
// P4 bluetooth service and the SPI bridge to the C5 radio, each gated by a
// capability. The C5 serialises the whole Bluetooth host, so every op holds the
// single RES_BLE lease: a call returns BUSY when the companion BLE link or
// another app owns the radio, and starting an attack can never tear down the
// companion channel out from under it.

#include "tos_api.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bluetooth_service.h"
#include "resource_mgr.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "tos_app_ctx.h"

#define NEED(cap)                   \
  do {                              \
    if (!tos_app_cap_check(cap))    \
      return ESP_ERR_NOT_SUPPORTED; \
  } while (0)

#define BLE_DEFAULT_SCAN_MS 5000
#define BLE_SCAN_SLACK_MS   2000

// One held BLE lease an app owns: a session attack (session id) or the HID
// keyboard (no session id). Tied to its RES_BLE grant so a preemption or the
// app's teardown stops the C5 activity and frees the radio.
#define BLE_APP_GRANTS 4

typedef struct {
  bool active;
  bool is_hid;      // true = BLE HID keyboard; false = a session attack
  uint32_t session; // valid when !is_hid
  res_handle_t handle;
  void *task;
} ble_grant_t;

static ble_grant_t s_grants[BLE_APP_GRANTS];
static SemaphoreHandle_t s_grants_lock = NULL;

static bool ensure_lock(void) {
  if (s_grants_lock == NULL)
    s_grants_lock = xSemaphoreCreateMutex();
  return s_grants_lock != NULL;
}

static void clear_record(ble_grant_t *g) {
  g->active = false;
  g->is_hid = false;
  g->session = SPI_SESSION_INVALID_ID;
  g->handle = RES_HANDLE_NONE;
  g->task = NULL;
}

static esp_err_t send_void(spi_id_t id, const uint8_t *pl, uint8_t len) {
  spi_header_t hdr = {0};
  uint8_t resp[8] = {0};
  return spi_bridge_send_command(id, pl, len, &hdr, resp, sizeof(resp), spi_bridge_get_timeout(id));
}

static void stop_c5_session(uint32_t session) {
  uint8_t pl[4];
  memcpy(pl, &session, 4);
  send_void(SPI_ID_SESSION_STOP, pl, sizeof(pl));
}

// resource_mgr fires this (outside its lock) when this app's RES_BLE grant is
// preempted or bulk-released on teardown. The grant is already gone; stop the C5
// activity so the radio is actually freed, then drop the record.
static void ble_grant_revoked(void *user) {
  ble_grant_t *g = (ble_grant_t *)user;
  bool is_hid = false;
  uint32_t session = SPI_SESSION_INVALID_ID;
  void *task = NULL;
  xSemaphoreTake(s_grants_lock, portMAX_DELAY);
  if (g->active) {
    is_hid = g->is_hid;
    session = g->session;
    task = g->task;
  }
  clear_record(g);
  xSemaphoreGive(s_grants_lock);
  tos_app_ctx_signal_resource_lost(task);
  if (is_hid)
    send_void(SPI_ID_BT_HID_DEINIT, NULL, 0);
  else if (session != SPI_SESSION_INVALID_ID)
    stop_c5_session(session);
}

// Reserve a free record and the RES_BLE lease for this app. Returns the record
// (with its handle set) or NULL, writing BUSY/NO_MEM to *out_err.
static ble_grant_t *acquire_lease(bool is_hid, esp_err_t *out_err) {
  if (!ensure_lock()) {
    *out_err = ESP_ERR_NO_MEM;
    return NULL;
  }
  xSemaphoreTake(s_grants_lock, portMAX_DELAY);
  ble_grant_t *g = NULL;
  for (int i = 0; i < BLE_APP_GRANTS; i++) {
    if (!s_grants[i].active) {
      g = &s_grants[i];
      clear_record(g);
      g->active = true;
      g->is_hid = is_hid;
      g->task = xTaskGetCurrentTaskHandle();
      break;
    }
  }
  xSemaphoreGive(s_grants_lock);
  if (g == NULL) {
    *out_err = ESP_ERR_NO_MEM;
    return NULL;
  }

  res_request_t req = {.id = RES_BLE,
                       .lane = RES_LANE_MAIN,
                       .owner_kind = RES_OWNER_APP,
                       .owner_task = g->task,
                       .allow_preempt = false, // apps never preempt the companion or each other
                       .on_revoke = ble_grant_revoked,
                       .user = g};
  res_handle_t handle = RES_HANDLE_NONE;
  if (resource_acquire(&req, &handle) != ESP_OK) {
    xSemaphoreTake(s_grants_lock, portMAX_DELAY);
    clear_record(g);
    xSemaphoreGive(s_grants_lock);
    *out_err = ESP_ERR_INVALID_STATE; // radio busy
    return NULL;
  }
  g->handle = handle;
  tos_app_ctx_clear_resource_lost(); // fresh lease: forget any earlier preemption
  *out_err = ESP_OK;
  return g;
}

static void release_lease(ble_grant_t *g) {
  res_handle_t handle = g->handle;
  xSemaphoreTake(s_grants_lock, portMAX_DELAY);
  clear_record(g);
  xSemaphoreGive(s_grants_lock);
  resource_release(handle);
}

// Start a session-based BLE attack, holding RES_BLE for the session's life.
static esp_err_t start_ble_session(spi_id_t id, const uint8_t *pl, uint8_t len,
                                   uint32_t *out_session) {
  if (out_session != NULL)
    *out_session = SPI_SESSION_INVALID_ID;
  esp_err_t err = ESP_OK;
  ble_grant_t *g = acquire_lease(false, &err);
  if (g == NULL)
    return err;

  spi_header_t hdr = {0};
  uint8_t resp[8] = {0};
  uint32_t session = SPI_SESSION_INVALID_ID;
  esp_err_t e =
      spi_bridge_send_command(id, pl, len, &hdr, resp, sizeof(resp), spi_bridge_get_timeout(id));
  if (e == ESP_OK && hdr.length >= 4)
    memcpy(&session, resp, 4);
  if (e != ESP_OK || session == SPI_SESSION_INVALID_ID) {
    release_lease(g);
    return (e != ESP_OK) ? e : ESP_FAIL;
  }

  // If we were preempted while the start was in flight, stop the orphan session.
  xSemaphoreTake(s_grants_lock, portMAX_DELAY);
  bool still_ours = g->active;
  if (still_ours)
    g->session = session;
  xSemaphoreGive(s_grants_lock);
  if (!still_ours) {
    stop_c5_session(session);
    return ESP_ERR_INVALID_STATE;
  }

  if (out_session != NULL)
    *out_session = session;
  return ESP_OK;
}

// One-shot radio op: refuse if the radio is held, otherwise send and free.
static esp_err_t send_momentary(spi_id_t id, const uint8_t *pl, uint8_t len) {
  esp_err_t err = ESP_OK;
  ble_grant_t *g = acquire_lease(false, &err);
  if (g == NULL)
    return err;
  esp_err_t e = send_void(id, pl, len);
  release_lease(g);
  return e;
}

// --- discovery -------------------------------------------------------------

static esp_err_t b_scan(uint32_t duration_ms) {
  NEED(TOS_CAP_RADIO_RX);
  if (duration_ms == 0)
    duration_ms = BLE_DEFAULT_SCAN_MS;
  esp_err_t err = ESP_OK;
  ble_grant_t *g = acquire_lease(false, &err);
  if (g == NULL)
    return err;

  bluetooth_service_scan(duration_ms);

  uint32_t waited = 0;
  uint32_t cap = duration_ms + BLE_SCAN_SLACK_MS;
  while (waited < cap) {
    vTaskDelay(pdMS_TO_TICKS(100));
    waited += 100;
    spi_header_t hdr = {0};
    uint8_t busy = 1;
    if (spi_bridge_send_command(SPI_ID_BT_SCAN_STATUS, NULL, 0, &hdr, &busy, 1,
                                spi_bridge_get_timeout(SPI_ID_BT_SCAN_STATUS)) == ESP_OK &&
        busy == 0)
      break;
  }
  release_lease(g);
  return ESP_OK;
}

static int b_device_count(void) {
  if (!tos_app_cap_check(TOS_CAP_RADIO_RX))
    return 0;
  return (int)bluetooth_service_get_scan_count();
}

static esp_err_t b_device_get(int index, tos_ble_device_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  if (out == NULL || index < 0)
    return ESP_ERR_INVALID_ARG;
  bluetooth_service_scan_result_t *r = bluetooth_service_get_scan_result((uint16_t)index);
  if (r == NULL)
    return ESP_ERR_NOT_FOUND;
  memset(out, 0, sizeof(*out));
  strlcpy(out->name, r->name, sizeof(out->name));
  memcpy(out->addr, r->addr, 6);
  out->addr_type = r->addr_type;
  out->rssi = (int8_t)r->rssi;
  return ESP_OK;
}

// --- basic radio control ---------------------------------------------------

static esp_err_t b_connect(const uint8_t addr[6], uint8_t addr_type) {
  NEED(TOS_CAP_RADIO_TX);
  if (addr == NULL)
    return ESP_ERR_INVALID_ARG;
  uint8_t pl[7];
  memcpy(pl, addr, 6);
  pl[6] = addr_type;
  return send_momentary(SPI_ID_BT_CONNECT, pl, sizeof(pl));
}

static esp_err_t b_disconnect(void) {
  NEED(TOS_CAP_RADIO_TX);
  return send_momentary(SPI_ID_BT_DISCONNECT, NULL, 0);
}

static esp_err_t b_set_random_mac(void) {
  NEED(TOS_CAP_RADIO_TX);
  return send_momentary(SPI_ID_BT_SET_RANDOM_MAC, NULL, 0);
}

static esp_err_t b_adv_start(void) {
  NEED(TOS_CAP_RADIO_TX);
  return send_momentary(SPI_ID_BT_START_ADV, NULL, 0);
}

static esp_err_t b_adv_stop(void) {
  NEED(TOS_CAP_RADIO_TX);
  return send_void(SPI_ID_BT_STOP_ADV, NULL, 0);
}

// --- session attacks -------------------------------------------------------

static esp_err_t b_sniffer_start(uint32_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  return start_ble_session(SPI_ID_BT_APP_SNIFFER, NULL, 0, out);
}

static esp_err_t b_spam(uint8_t attack_index, uint32_t *out) {
  NEED(TOS_CAP_RADIO_TX);
  return start_ble_session(SPI_ID_BT_APP_SPAM, &attack_index, 1, out);
}

static esp_err_t b_flood(const uint8_t addr[6], uint8_t addr_type, uint32_t *out) {
  NEED(TOS_CAP_RADIO_TX);
  if (addr == NULL)
    return ESP_ERR_INVALID_ARG;
  uint8_t pl[7];
  memcpy(pl, addr, 6);
  pl[6] = addr_type;
  return start_ble_session(SPI_ID_BT_APP_FLOOD, pl, sizeof(pl), out);
}

static esp_err_t b_skimmer_detect(uint32_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  return start_ble_session(SPI_ID_BT_APP_SKIMMER, NULL, 0, out);
}

static esp_err_t b_tracker_detect(uint32_t *out) {
  NEED(TOS_CAP_RADIO_RX);
  return start_ble_session(SPI_ID_BT_APP_TRACKER, NULL, 0, out);
}

static esp_err_t b_session_stop(uint32_t session) {
  res_handle_t handle = RES_HANDLE_NONE;
  if (ensure_lock()) {
    xSemaphoreTake(s_grants_lock, portMAX_DELAY);
    for (int i = 0; i < BLE_APP_GRANTS; i++) {
      if (s_grants[i].active && !s_grants[i].is_hid && s_grants[i].session == session) {
        handle = s_grants[i].handle;
        clear_record(&s_grants[i]);
        break;
      }
    }
    xSemaphoreGive(s_grants_lock);
  }
  resource_release(handle);
  stop_c5_session(session);
  return ESP_OK;
}

// --- BLE HID keyboard (BadBLE) ---------------------------------------------

static esp_err_t b_hid_start(void) {
  NEED(TOS_CAP_HID);
  esp_err_t err = ESP_OK;
  ble_grant_t *g = acquire_lease(true, &err);
  if (g == NULL)
    return err;
  esp_err_t e = send_void(SPI_ID_BT_HID_INIT, NULL, 0);
  if (e != ESP_OK) {
    release_lease(g);
    return e;
  }
  return ESP_OK;
}

static esp_err_t b_hid_stop(void) {
  NEED(TOS_CAP_HID);
  if (ensure_lock()) {
    xSemaphoreTake(s_grants_lock, portMAX_DELAY);
    for (int i = 0; i < BLE_APP_GRANTS; i++) {
      if (s_grants[i].active && s_grants[i].is_hid) {
        res_handle_t handle = s_grants[i].handle;
        clear_record(&s_grants[i]);
        xSemaphoreGive(s_grants_lock);
        resource_release(handle);
        return send_void(SPI_ID_BT_HID_DEINIT, NULL, 0);
      }
    }
    xSemaphoreGive(s_grants_lock);
  }
  return send_void(SPI_ID_BT_HID_DEINIT, NULL, 0);
}

static bool b_hid_is_connected(void) {
  if (!tos_app_cap_check(TOS_CAP_HID))
    return false;
  spi_header_t hdr = {0};
  uint8_t connected = 0;
  if (spi_bridge_send_command(SPI_ID_BT_HID_IS_CONNECTED, NULL, 0, &hdr, &connected, 1,
                              spi_bridge_get_timeout(SPI_ID_BT_HID_IS_CONNECTED)) != ESP_OK)
    return false;
  return connected != 0;
}

static esp_err_t b_hid_send_key(uint8_t modifier, uint8_t keycode) {
  NEED(TOS_CAP_HID);
  uint8_t pl[2] = {modifier, keycode};
  return send_void(SPI_ID_BT_HID_SEND_KEY, pl, sizeof(pl));
}

const tos_ble_api_t tos_ble_api_impl = {
    .scan = b_scan,
    .device_count = b_device_count,
    .device_get = b_device_get,
    .connect = b_connect,
    .disconnect = b_disconnect,
    .set_random_mac = b_set_random_mac,
    .adv_start = b_adv_start,
    .adv_stop = b_adv_stop,
    .sniffer_start = b_sniffer_start,
    .spam = b_spam,
    .flood = b_flood,
    .skimmer_detect = b_skimmer_detect,
    .tracker_detect = b_tracker_detect,
    .session_stop = b_session_stop,
    .hid_start = b_hid_start,
    .hid_stop = b_hid_stop,
    .hid_is_connected = b_hid_is_connected,
    .hid_send_key = b_hid_send_key,
};
