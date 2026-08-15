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

#include "bluetooth_service.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_session.h"

static const char *TAG = "BT_SERVICE_P4";

#define SPI_TIMEOUT_MS         2000
#define SPI_DATA_TIMEOUT_MS    1000
#define SPI_CONNECT_TIMEOUT_MS 5000
#define SPI_SCAN_TIMEOUT_MS    10000
#define BLE_MAC_LEN            6

static bluetooth_service_sniffer_cb_t s_sniffer_cb = NULL;
static uint32_t s_sniffer_session = SPI_SESSION_INVALID_ID;
static bluetooth_service_scan_result_t s_cached_scan_record;

static bool get_info(spi_bt_info_t *out_info);

// Public function implementations

esp_err_t bluetooth_service_init(void) {
  return spi_bridge_send_command(SPI_ID_BT_INIT, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

esp_err_t bluetooth_service_deinit(void) {
  return spi_bridge_send_command(SPI_ID_BT_DEINIT, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

esp_err_t bluetooth_service_start(void) {
  return spi_bridge_send_command(SPI_ID_BT_START, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

esp_err_t bluetooth_service_stop(void) {
  return spi_bridge_send_command(SPI_ID_BT_STOP, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

bool bluetooth_service_is_initialized(void) {
  spi_bt_info_t info = {0};
  return get_info(&info) ? (info.initialized != 0) : false;
}

bool bluetooth_service_is_running(void) {
  spi_bt_info_t info = {0};
  return get_info(&info) ? (info.running != 0) : false;
}

void bluetooth_service_disconnect_all(void) {
  spi_bridge_send_command(SPI_ID_BT_DISCONNECT, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

int bluetooth_service_get_connected_count(void) {
  spi_bt_info_t info = {0};
  return get_info(&info) ? (int)info.connected_count : 0;
}

void bluetooth_service_get_mac(uint8_t *out_mac) {
  if (out_mac == NULL) {
    return;
  }
  spi_bt_info_t info = {0};
  if (get_info(&info)) {
    memcpy(out_mac, info.mac, BLE_MAC_LEN);
  } else {
    memset(out_mac, 0, BLE_MAC_LEN);
  }
}

esp_err_t bluetooth_service_set_random_mac(void) {
  return spi_bridge_send_command(SPI_ID_BT_SET_RANDOM_MAC, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

esp_err_t bluetooth_service_connect(const uint8_t *addr,
                                    uint8_t addr_type,
                                    int (*cb)(struct ble_gap_event *event, void *arg)) {
  (void)cb;
  if (addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t payload[7];
  memcpy(payload, addr, BLE_MAC_LEN);
  payload[6] = addr_type;
  return spi_bridge_send_command(
      SPI_ID_BT_CONNECT, payload, sizeof(payload), NULL, NULL, 0, SPI_CONNECT_TIMEOUT_MS);
}

static void sniffer_session_stream(const uint8_t *payload, uint8_t len) {
  if (s_sniffer_cb == NULL || payload == NULL || len < sizeof(spi_ble_sniffer_frame_t))
    return;
  const spi_ble_sniffer_frame_t *frame = (const spi_ble_sniffer_frame_t *)payload;
  s_sniffer_cb(frame->addr, frame->addr_type, frame->rssi, frame->data, frame->len);
}

static void sniffer_session_lost(uint32_t session_id, spi_id_t op_id) {
  (void)op_id;
  if (session_id == s_sniffer_session) {
    s_sniffer_session = SPI_SESSION_INVALID_ID;
    s_sniffer_cb = NULL;
  }
}

esp_err_t bluetooth_service_start_sniffer(bluetooth_service_sniffer_cb_t cb) {
  s_sniffer_cb = cb;
  s_sniffer_session = spi_session_start(
      SPI_ID_BT_APP_SNIFFER, NULL, 0, sniffer_session_stream, sniffer_session_lost);
  if (s_sniffer_session == SPI_SESSION_INVALID_ID) {
    s_sniffer_cb = NULL;
    return ESP_FAIL;
  }
  return ESP_OK;
}

void bluetooth_service_stop_sniffer(void) {
  if (s_sniffer_session != SPI_SESSION_INVALID_ID) {
    spi_session_stop(s_sniffer_session);
    s_sniffer_session = SPI_SESSION_INVALID_ID;
  }
  s_sniffer_cb = NULL;
}

esp_err_t bluetooth_service_start_tracker(const uint8_t *addr, bluetooth_service_tracker_cb_t cb) {
  (void)cb;
  if (addr == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return spi_bridge_send_command(
      SPI_ID_BT_TRACKER_START, addr, BLE_MAC_LEN, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

void bluetooth_service_stop_tracker(void) {
  spi_bridge_send_command(SPI_ID_BT_TRACKER_STOP, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

esp_err_t bluetooth_service_start_advertising(void) {
  return spi_bridge_send_command(SPI_ID_BT_START_ADV, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

esp_err_t bluetooth_service_stop_advertising(void) {
  return spi_bridge_send_command(SPI_ID_BT_STOP_ADV, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

uint8_t bluetooth_service_get_own_addr_type(void) {
  spi_header_t resp;
  uint8_t payload[1] = {0};
  if (spi_bridge_send_command(
          SPI_ID_BT_GET_ADDR_TYPE, NULL, 0, &resp, payload, sizeof(payload), SPI_DATA_TIMEOUT_MS) == ESP_OK) {
    return payload[0];
  }
  return 0;
}

esp_err_t bluetooth_service_set_max_power(void) {
  return spi_bridge_send_command(SPI_ID_BT_SET_MAX_POWER, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

esp_err_t bluetooth_service_save_announce_config(const char *name, uint8_t max_conn) {
  if (name == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  spi_bt_announce_config_t cfg = {0};
  strncpy(cfg.name, name, sizeof(cfg.name) - 1);
  cfg.max_conn = max_conn;
  return spi_bridge_send_command(
      SPI_ID_BT_SAVE_ANNOUNCE_CFG, (uint8_t *)&cfg, sizeof(cfg), NULL, NULL, 0, SPI_TIMEOUT_MS);
}

esp_err_t bluetooth_service_load_spam_list(char ***out_list, size_t *out_count) {
  if (out_list == NULL || out_count == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  *out_list = NULL;
  *out_count = 0;

  esp_err_t err =
      spi_bridge_send_command(SPI_ID_BT_SPAM_LIST_LOAD, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
  if (err != ESP_OK) {
    return err;
  }

  spi_header_t resp;
  uint8_t payload[2];
  uint16_t magic_count = SPI_DATA_INDEX_COUNT;
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_DATA, (uint8_t *)&magic_count, 2, &resp, payload, sizeof(payload), SPI_DATA_TIMEOUT_MS) !=
      ESP_OK) {
    return ESP_FAIL;
  }

  uint16_t total = 0;
  memcpy(&total, payload, 2);
  if (total == 0) {
    return ESP_OK;
  }

  if (total > SPI_BT_SPAM_LIST_MAX) {
    total = SPI_BT_SPAM_LIST_MAX;
  }

  char **out = (char **)calloc(total, sizeof(char *));
  if (out == NULL) {
    return ESP_ERR_NO_MEM;
  }

  for (uint16_t i = 0; i < total; i++) {
    uint16_t idx = i;
    uint8_t item_buf[SPI_BT_SPAM_ITEM_LEN] = {0};
    if (spi_bridge_send_command(
            SPI_ID_SYSTEM_DATA, (uint8_t *)&idx, 2, &resp, item_buf, sizeof(item_buf), SPI_DATA_TIMEOUT_MS) !=
        ESP_OK) {
      bluetooth_service_free_spam_list(out, i);
      return ESP_FAIL;
    }
    item_buf[SPI_BT_SPAM_ITEM_LEN - 1] = '\0';
    out[i] = strdup((char *)item_buf);
    if (out[i] == NULL) {
      bluetooth_service_free_spam_list(out, i);
      return ESP_ERR_NO_MEM;
    }
  }

  *out_list = out;
  *out_count = total;
  return ESP_OK;
}

esp_err_t bluetooth_service_save_spam_list(const char *const *list, size_t count) {
  if (list == NULL || count == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (count > SPI_BT_SPAM_LIST_MAX) {
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t total = (uint16_t)count;
  esp_err_t err = spi_bridge_send_command(
      SPI_ID_BT_SPAM_LIST_BEGIN, (uint8_t *)&total, sizeof(total), NULL, NULL, 0, SPI_TIMEOUT_MS);
  if (err != ESP_OK) {
    return err;
  }

  for (uint16_t i = 0; i < total; i++) {
    uint8_t payload[2 + SPI_BT_SPAM_ITEM_LEN];
    memcpy(payload, &i, 2);
    memset(payload + 2, 0, SPI_BT_SPAM_ITEM_LEN);
    if (list[i] != NULL) {
      strncpy((char *)(payload + 2), list[i], SPI_BT_SPAM_ITEM_LEN - 1);
    }
    err = spi_bridge_send_command(
        SPI_ID_BT_SPAM_LIST_ITEM, payload, sizeof(payload), NULL, NULL, 0, SPI_TIMEOUT_MS);
    if (err != ESP_OK) {
      return err;
    }
  }

  return spi_bridge_send_command(SPI_ID_BT_SPAM_LIST_COMMIT, NULL, 0, NULL, NULL, 0, SPI_TIMEOUT_MS);
}

void bluetooth_service_free_spam_list(char **list, size_t count) {
  if (list == NULL) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    free(list[i]);
  }
  free(list);
}

void bluetooth_service_scan(uint32_t duration_ms) {
  uint8_t payload[4];
  memcpy(payload, &duration_ms, sizeof(duration_ms));
  spi_bridge_run_scan(SPI_ID_BT_SCAN, SPI_ID_BT_SCAN_STATUS, payload, sizeof(payload));
}

uint16_t bluetooth_service_get_scan_count(void) {
  spi_header_t resp;
  uint8_t payload[2];
  uint16_t magic_count = SPI_DATA_INDEX_COUNT;
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_DATA, (uint8_t *)&magic_count, 2, &resp, payload, sizeof(payload), SPI_DATA_TIMEOUT_MS) ==
      ESP_OK) {
    uint16_t count;
    memcpy(&count, payload, 2);
    return count;
  }
  return 0;
}

bluetooth_service_scan_result_t *bluetooth_service_get_scan_result(uint16_t index) {
  spi_header_t resp;
  if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                              (uint8_t *)&index,
                              2,
                              &resp,
                              (uint8_t *)&s_cached_scan_record, sizeof(s_cached_scan_record),
                              SPI_DATA_TIMEOUT_MS) == ESP_OK) {
    return &s_cached_scan_record;
  }
  return NULL;
}

// Static function implementations

static bool get_info(spi_bt_info_t *out_info) {
  if (out_info == NULL) {
    return false;
  }
  spi_header_t resp;
  if (spi_bridge_send_command(
          SPI_ID_BT_GET_INFO, NULL, 0, &resp, (uint8_t *)out_info, sizeof(*out_info), SPI_DATA_TIMEOUT_MS) == ESP_OK) {
    return true;
  }
  return false;
}
