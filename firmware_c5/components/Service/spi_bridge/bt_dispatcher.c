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

#include "bt_dispatcher.h"

#include <string.h>

#include "esp_log.h"

#include "ble_connect_flood.h"
#include "ble_hid_keyboard.h"
#include "ble_l2cap_flood.h"
#include "ble_scanner.h"
#include "ble_sniffer.h"
#include "canned_spam.h"
#include "gatt_explorer.h"
#include "session_manager.h"
#include "bluetooth_service.h"
#include "host_link_gatt.h"
#include "host_transport.h"
#include "meshcore_gatt.h"
#include "meshcore_transport.h"
#include "meshtastic_gatt.h"
#include "meshtastic_transport.h"
#include "skimmer_detector.h"
#include "spi_bridge.h"
#include "tracker_detector.h"

static const char *TAG = "BT_DISPATCHER";

#define BT_SCAN_DEFAULT_DURATION_MS 5000
#define BT_CONNECT_MIN_PAYLOAD      7
#define BT_MAC_LEN                  6
#define BT_SPAM_ITEM_STAGE_MIN      2 // [index u16] prefix on a spam-list item

// Staging buffer for the spam name list: reused for LOAD (served through the
// data pipe) and for BEGIN/ITEM/COMMIT (accumulate then persist). Only one of
// those flows runs at a time.
static char s_bt_spam_items[SPI_BT_SPAM_LIST_MAX][SPI_BT_SPAM_ITEM_LEN];
static uint16_t s_bt_spam_staged; // total announced by BEGIN

static void killed_ble_flood(spi_id_t id) {
  (void)id;
  ble_connect_flood_stop();
}
static void killed_skimmer(spi_id_t id) {
  (void)id;
  skimmer_detector_stop();
}
static void killed_tracker(spi_id_t id) {
  (void)id;
  tracker_detector_stop();
}
static void killed_spam(spi_id_t id) {
  (void)id;
  spam_stop();
}

static bool bt_ensure_service_ready(void) {
  if (meshcore_gatt_is_running())
    meshcore_gatt_stop();
  if (host_link_gatt_is_running())
    host_link_gatt_stop();
  if (meshtastic_gatt_is_running())
    meshtastic_gatt_stop();
  if (!bluetooth_service_is_initialized() && bluetooth_service_init() != ESP_OK)
    return false;
  if (!bluetooth_service_is_running() && bluetooth_service_start() != ESP_OK)
    return false;
  bluetooth_service_stop_advertising();
  return true;
}

static spi_status_t bt_release_service_for_gatt(void) {
  if (!bluetooth_service_is_initialized())
    return SPI_STATUS_OK;
  if (spi_bridge_async_scan_busy() || session_manager_is_active())
    return SPI_STATUS_BUSY;
  return (bluetooth_service_deinit() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
}

static spi_status_t bt_open_session(spi_id_t op_id,
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

// BLE scan work function run by the shared async runner. The duration is stashed
// by the SPI handler before it kicks the runner (one scan at a time).
static uint32_t s_bt_scan_duration = 0;

static void scan_fn_bt(void) {
  bluetooth_service_scan(s_bt_scan_duration);
  spi_bridge_provide_results(bluetooth_service_get_scan_result(0),
                             bluetooth_service_get_scan_count(),
                             sizeof(bluetooth_service_scan_result_t));
}

spi_status_t bt_dispatcher_execute(spi_id_t id,
                                   const uint8_t *payload,
                                   uint8_t len,
                                   uint8_t *out_resp_payload,
                                   uint8_t *out_resp_len) {
  (void)TAG;
  *out_resp_len = 0;

  switch (id) {
    case SPI_ID_BT_INIT:
      if (meshcore_gatt_is_running())
        meshcore_gatt_stop();
      if (host_link_gatt_is_running())
        host_link_gatt_stop();
      if (meshtastic_gatt_is_running())
        meshtastic_gatt_stop();
      return (bluetooth_service_init() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_DEINIT:
      return (bluetooth_service_deinit() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_START:
      return (bluetooth_service_start() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_STOP:
      return (bluetooth_service_deinit() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_SCAN: {
      uint32_t duration = BT_SCAN_DEFAULT_DURATION_MS;
      if (len >= sizeof(duration))
        memcpy(&duration, payload, sizeof(duration));
      s_bt_scan_duration = duration;
      return spi_bridge_async_scan_start(scan_fn_bt) ? SPI_STATUS_OK : SPI_STATUS_BUSY;
    }

    case SPI_ID_BT_SCAN_STATUS:
      out_resp_payload[0] = spi_bridge_async_scan_busy() ? 1 : 0;
      *out_resp_len = 1;
      return SPI_STATUS_OK;

    case SPI_ID_BT_CONNECT: {
      if (len < BT_CONNECT_MIN_PAYLOAD)
        return SPI_STATUS_ERROR;
      return (bluetooth_service_connect(payload, payload[BT_MAC_LEN], NULL) == ESP_OK)
                 ? SPI_STATUS_OK
                 : SPI_STATUS_ERROR;
    }

    case SPI_ID_BT_DISCONNECT:
      bluetooth_service_disconnect_all();
      return SPI_STATUS_OK;

    case SPI_ID_BT_GET_INFO: {
      spi_bt_info_t info = {0};
      bluetooth_service_get_mac(info.mac);
      info.running = bluetooth_service_is_running() ? 1 : 0;
      info.initialized = bluetooth_service_is_initialized() ? 1 : 0;
      info.connected_count = (uint16_t)bluetooth_service_get_connected_count();
      memcpy(out_resp_payload, &info, sizeof(info));
      *out_resp_len = sizeof(info);
      return SPI_STATUS_OK;
    }

    case SPI_ID_BT_SET_RANDOM_MAC:
      return (bluetooth_service_set_random_mac() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_START_ADV:
      return (bluetooth_service_start_advertising() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_STOP_ADV:
      return (bluetooth_service_stop_advertising() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_SET_MAX_POWER:
      return (bluetooth_service_set_max_power() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_GET_ADDR_TYPE:
      out_resp_payload[0] = bluetooth_service_get_own_addr_type();
      *out_resp_len = 1;
      return SPI_STATUS_OK;

    case SPI_ID_BT_TRACKER_START:
      if (len < BT_MAC_LEN)
        return SPI_STATUS_INVALID_ARG;
      return (bluetooth_service_start_tracker(payload, NULL) == ESP_OK) ? SPI_STATUS_OK
                                                                        : SPI_STATUS_ERROR;

    case SPI_ID_BT_TRACKER_STOP:
      bluetooth_service_stop_tracker();
      return SPI_STATUS_OK;

    case SPI_ID_BT_SAVE_ANNOUNCE_CFG: {
      if (len < sizeof(spi_bt_announce_config_t))
        return SPI_STATUS_INVALID_ARG;
      spi_bt_announce_config_t cfg;
      memcpy(&cfg, payload, sizeof(cfg));
      cfg.name[sizeof(cfg.name) - 1] = '\0';
      return (bluetooth_service_save_announce_config(cfg.name, cfg.max_conn) == ESP_OK)
                 ? SPI_STATUS_OK
                 : SPI_STATUS_ERROR;
    }

    case SPI_ID_BT_APP_GATT_EXP: {
      if (len < BT_CONNECT_MIN_PAYLOAD)
        return SPI_STATUS_INVALID_ARG;
      if (!bt_ensure_service_ready())
        return SPI_STATUS_ERROR;
      return gatt_explorer_start(payload, payload[BT_MAC_LEN]) ? SPI_STATUS_OK : SPI_STATUS_BUSY;
    }

    case SPI_ID_BT_SPAM_LIST_LOAD: {
      char **list = NULL;
      size_t count = 0;
      if (bluetooth_service_load_spam_list(&list, &count) != ESP_OK)
        return SPI_STATUS_ERROR;
      uint16_t served = (count > SPI_BT_SPAM_LIST_MAX) ? SPI_BT_SPAM_LIST_MAX : (uint16_t)count;
      for (uint16_t i = 0; i < served; i++) {
        memset(s_bt_spam_items[i], 0, SPI_BT_SPAM_ITEM_LEN);
        if (list[i] != NULL)
          strncpy(s_bt_spam_items[i], list[i], SPI_BT_SPAM_ITEM_LEN - 1);
      }
      bluetooth_service_free_spam_list(list, count);
      spi_bridge_provide_results(s_bt_spam_items, served, SPI_BT_SPAM_ITEM_LEN);
      return SPI_STATUS_OK;
    }

    case SPI_ID_BT_SPAM_LIST_BEGIN: {
      if (len < 2)
        return SPI_STATUS_INVALID_ARG;
      uint16_t total = (uint16_t)(payload[0] | (payload[1] << 8));
      if (total > SPI_BT_SPAM_LIST_MAX)
        total = SPI_BT_SPAM_LIST_MAX;
      s_bt_spam_staged = total;
      memset(s_bt_spam_items, 0, sizeof(s_bt_spam_items));
      return SPI_STATUS_OK;
    }

    case SPI_ID_BT_SPAM_LIST_ITEM: {
      if (len < BT_SPAM_ITEM_STAGE_MIN)
        return SPI_STATUS_INVALID_ARG;
      uint16_t idx = (uint16_t)(payload[0] | (payload[1] << 8));
      if (idx >= s_bt_spam_staged)
        return SPI_STATUS_INVALID_ARG;
      uint8_t name_len = (uint8_t)(len - BT_SPAM_ITEM_STAGE_MIN);
      if (name_len >= SPI_BT_SPAM_ITEM_LEN)
        name_len = SPI_BT_SPAM_ITEM_LEN - 1;
      memset(s_bt_spam_items[idx], 0, SPI_BT_SPAM_ITEM_LEN);
      memcpy(s_bt_spam_items[idx], payload + BT_SPAM_ITEM_STAGE_MIN, name_len);
      return SPI_STATUS_OK;
    }

    case SPI_ID_BT_SPAM_LIST_COMMIT: {
      const char *ptrs[SPI_BT_SPAM_LIST_MAX];
      for (uint16_t i = 0; i < s_bt_spam_staged; i++)
        ptrs[i] = s_bt_spam_items[i];
      esp_err_t err = bluetooth_service_save_spam_list(ptrs, s_bt_spam_staged);
      return (err == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_BT_HID_INIT:
      // Bring the NimBLE host up first (and tear down mesh/host GATT). Without
      // this, HID_INIT after the stack was deinitialised (e.g. companion BLE
      // turned off) runs ble_gatts_count_cfg on a dead host and crashes.
      if (!bt_ensure_service_ready())
        return SPI_STATUS_ERROR;
      return (ble_hid_init() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_HID_DEINIT:
      return (ble_hid_deinit() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_HID_IS_CONNECTED:
      out_resp_payload[0] = ble_hid_is_connected() ? 1 : 0;
      *out_resp_len = 1;
      return SPI_STATUS_OK;

    case SPI_ID_BT_HID_SEND_KEY:
      if (len < 2)
        return SPI_STATUS_INVALID_ARG;
      ble_hid_send_key(payload[1], payload[0]); // payload = [modifier][keycode]
      return SPI_STATUS_OK;

    case SPI_ID_BT_L2CAP_STATUS:
      out_resp_payload[0] = ble_l2cap_flood_is_running() ? 1 : 0;
      *out_resp_len = 1;
      return SPI_STATUS_OK;

    case SPI_ID_BT_APP_SCANNER:
      if (!bt_ensure_service_ready())
        return SPI_STATUS_ERROR;
      return ble_scanner_start() ? SPI_STATUS_OK : SPI_STATUS_BUSY;

    case SPI_ID_BT_APP_SNIFFER: {
      if (!bt_ensure_service_ready())
        return SPI_STATUS_ERROR;
      if (ble_sniffer_start() != ESP_OK)
        return SPI_STATUS_ERROR;
      uint32_t sid = session_manager_start(SPI_ID_BT_APP_SNIFFER, ble_sniffer_session_killed);
      if (sid == SPI_SESSION_INVALID_ID) {
        ble_sniffer_stop();
        return SPI_STATUS_ERROR;
      }
      ble_sniffer_bind_session(sid);
      spi_session_resp_t resp = {.session_id = sid};
      memcpy(out_resp_payload, &resp, sizeof(resp));
      *out_resp_len = sizeof(resp);
      return SPI_STATUS_OK;
    }

    case SPI_ID_BT_APP_SPAM: {
      if (len < 1)
        return SPI_STATUS_INVALID_ARG;
      if (!bt_ensure_service_ready())
        return SPI_STATUS_ERROR;
      if (spam_start((int)payload[0]) != ESP_OK)
        return SPI_STATUS_ERROR;
      uint32_t sid = session_manager_start(SPI_ID_BT_APP_SPAM, killed_spam);
      if (sid == SPI_SESSION_INVALID_ID) {
        spam_stop();
        return SPI_STATUS_ERROR;
      }
      spi_session_resp_t resp = {.session_id = sid};
      memcpy(out_resp_payload, &resp, sizeof(resp));
      *out_resp_len = sizeof(resp);
      return SPI_STATUS_OK;
    }

    case SPI_ID_BT_APP_FLOOD: {
      if (len < BT_CONNECT_MIN_PAYLOAD)
        return SPI_STATUS_ERROR;
      if (!bt_ensure_service_ready())
        return SPI_STATUS_ERROR;
      if (ble_connect_flood_start(payload, payload[BT_MAC_LEN]) != ESP_OK)
        return SPI_STATUS_ERROR;
      uint32_t sid = session_manager_start(SPI_ID_BT_APP_FLOOD, killed_ble_flood);
      if (sid == SPI_SESSION_INVALID_ID) {
        ble_connect_flood_stop();
        return SPI_STATUS_ERROR;
      }
      spi_session_resp_t resp = {.session_id = sid};
      memcpy(out_resp_payload, &resp, sizeof(resp));
      *out_resp_len = sizeof(resp);
      return SPI_STATUS_OK;
    }

    case SPI_ID_BT_APP_SKIMMER:
      if (!bt_ensure_service_ready())
        return SPI_STATUS_ERROR;
      if (skimmer_detector_start() != ESP_OK)
        return SPI_STATUS_ERROR;
      return bt_open_session(SPI_ID_BT_APP_SKIMMER,
                             killed_skimmer,
                             out_resp_payload,
                             out_resp_len,
                             skimmer_detector_stop);

    case SPI_ID_BT_APP_TRACKER:
      if (!bt_ensure_service_ready())
        return SPI_STATUS_ERROR;
      if (tracker_detector_start() != ESP_OK)
        return SPI_STATUS_ERROR;
      return bt_open_session(SPI_ID_BT_APP_TRACKER,
                             killed_tracker,
                             out_resp_payload,
                             out_resp_len,
                             tracker_detector_stop);

    case SPI_ID_MESH_BLE_INIT: {
      if (len < sizeof(spi_mesh_init_t)) {
        return SPI_STATUS_INVALID_ARG;
      }
      spi_mesh_init_t req;
      memcpy(&req, payload, sizeof(req));
      spi_status_t rel = bt_release_service_for_gatt();
      if (rel != SPI_STATUS_OK) {
        return rel;
      }
      if (meshcore_gatt_is_running())
        meshcore_gatt_stop();
      if (host_link_gatt_is_running())
        host_link_gatt_stop();
      if (meshtastic_transport_init() != ESP_OK) {
        return SPI_STATUS_ERROR;
      }
      esp_err_t ret = meshtastic_gatt_init(req.node_num);
      if (ret == ESP_ERR_INVALID_STATE) {
        return SPI_STATUS_OK;
      }
      return (ret == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_MESH_BLE_STOP:
      meshtastic_gatt_stop();
      return SPI_STATUS_OK;

    case SPI_ID_MESH_FROMRADIO_PUSH:
      meshtastic_transport_inject_fromradio_chunk(payload, len);
      return SPI_STATUS_OK;

    case SPI_ID_MESH_LOG_PUSH:
      meshtastic_transport_inject_log_chunk(payload, len);
      return SPI_STATUS_OK;

    case SPI_ID_MESH_STATUS: {
      spi_mesh_status_t status;
      meshtastic_transport_get_status(&status);
      memcpy(out_resp_payload, &status, sizeof(status));
      *out_resp_len = sizeof(status);
      return SPI_STATUS_OK;
    }

    case SPI_ID_MCORE_BLE_INIT: {
      if (len < sizeof(spi_mcore_init_t)) {
        return SPI_STATUS_INVALID_ARG;
      }
      spi_mcore_init_t req;
      memcpy(&req, payload, sizeof(req));
      req.name_prefix[sizeof(req.name_prefix) - 1] = '\0';
      spi_status_t rel = bt_release_service_for_gatt();
      if (rel != SPI_STATUS_OK) {
        return rel;
      }
      if (meshtastic_gatt_is_running())
        meshtastic_gatt_stop();
      if (host_link_gatt_is_running())
        host_link_gatt_stop();
      if (meshcore_transport_init() != ESP_OK) {
        return SPI_STATUS_ERROR;
      }
      esp_err_t ret = meshcore_gatt_init(req.name_prefix, req.pin);
      if (ret == ESP_ERR_INVALID_STATE) {
        return SPI_STATUS_OK;
      }
      return (ret == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_MCORE_BLE_STOP:
      meshcore_gatt_stop();
      return SPI_STATUS_OK;

    case SPI_ID_MCORE_TX_PUSH:
      meshcore_transport_inject_tx_chunk(payload, len);
      return SPI_STATUS_OK;

    case SPI_ID_MCORE_STATUS: {
      spi_mcore_status_t status;
      meshcore_transport_get_status(&status);
      memcpy(out_resp_payload, &status, sizeof(status));
      *out_resp_len = sizeof(status);
      return SPI_STATUS_OK;
    }

    case SPI_ID_HOST_BLE_INIT: {
      if (len < sizeof(spi_host_init_t)) {
        return SPI_STATUS_INVALID_ARG;
      }
      spi_host_init_t req;
      memcpy(&req, payload, sizeof(req));
      req.name_prefix[sizeof(req.name_prefix) - 1] = '\0';
      spi_status_t rel = bt_release_service_for_gatt();
      if (rel != SPI_STATUS_OK) {
        return rel;
      }
      if (meshcore_gatt_is_running())
        meshcore_gatt_stop();
      if (meshtastic_gatt_is_running())
        meshtastic_gatt_stop();
      if (host_transport_init() != ESP_OK) {
        return SPI_STATUS_ERROR;
      }
      esp_err_t ret = host_link_gatt_init(req.name_prefix);
      if (ret == ESP_ERR_INVALID_STATE) {
        return SPI_STATUS_OK;
      }
      return (ret == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_HOST_BLE_STOP:
      host_link_gatt_stop();
      return SPI_STATUS_OK;

    case SPI_ID_HOST_TX:
      host_transport_inject_tx_chunk(payload, len);
      return SPI_STATUS_OK;

    case SPI_ID_HOST_STATUS: {
      spi_host_status_t status;
      host_transport_get_status(&status);
      memcpy(out_resp_payload, &status, sizeof(status));
      *out_resp_len = sizeof(status);
      return SPI_STATUS_OK;
    }

    default:
      return SPI_STATUS_ERROR;
  }
}
