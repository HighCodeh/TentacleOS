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

#include <assert.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_hs_stop.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/dis/ble_svc_dis.h"
#include "services/gap/ble_svc_gap.h"

#include "cJSON.h"
#include "storage_assets.h"
#include "storage_write.h"

static const char *TAG = "BLE_SERVICE";

#define BLE_ANNOUNCE_CONFIG_FILE "config/bluetooth/ble_announce.conf"
#define BLE_ANNOUNCE_CONFIG_PATH "/assets/" BLE_ANNOUNCE_CONFIG_FILE
#define BLE_SPAM_LIST_FILE       "config/bluetooth/beacon_list.conf"
#define BLE_SPAM_LIST_PATH       "/assets/" BLE_SPAM_LIST_FILE
#define MAX_BLE_CONNECTIONS      8
#define BLE_SYNC_TIMEOUT_MS      2000
#define BLE_CONNECT_TIMEOUT_MS   30000
#define BLE_NAME_MAX_LEN         31
#define BLE_MAC_LEN              6
#define BLE_RANDOM_ADDR_MSB      0xC0
#define BLE_SCAN_ITVL            0x0010
#define BLE_SCAN_WINDOW          0x0010
#define BLE_SUPERVISION_TIMEOUT  0x0100
#define BLE_MIN_CE_LEN           0x0010
#define BLE_MAX_CE_LEN           0x0300
#define DEFAULT_MAX_CONN         4

static uint8_t s_own_addr_type;
static bool s_is_initialized = false;
static bool s_is_running = false;
static SemaphoreHandle_t s_hs_synced_sem = NULL;
static uint16_t s_conn_handles[MAX_BLE_CONNECTIONS];
static int s_conn_count = 0;
static bluetooth_service_sniffer_cb_t s_sniffer_cb = NULL;
static bluetooth_service_tracker_cb_t s_tracker_cb = NULL;
static uint8_t s_tracker_target_addr[BLE_MAC_LEN];
static bluetooth_service_scan_result_t s_scan_results[BLE_SCAN_LIST_SIZE];
static uint16_t s_scan_count = 0;
static SemaphoreHandle_t s_scan_sem = NULL;

static void on_reset(int reason);
static void on_sync(void);
static void host_task(void *param);
static int gap_event(struct ble_gap_event *event, void *arg);
static void load_announce_config(char *out_name, uint8_t *out_max_conn);

// Public function implementations

esp_err_t bluetooth_service_init(void) {
  if (s_is_initialized) {
    ESP_LOGW(TAG, "BLE already initialized");
    return ESP_OK;
  }

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize NimBLE: %s", esp_err_to_name(ret));
    return ret;
  }

  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  char device_name[32] = "Darth Maul";
  uint8_t max_conn = DEFAULT_MAX_CONN;
  load_announce_config(device_name, &max_conn);

  ret = ble_svc_gap_device_name_set(device_name);
  assert(ret == 0);

  s_hs_synced_sem = xSemaphoreCreateBinary();
  if (s_hs_synced_sem == NULL) {
    ESP_LOGE(TAG, "Failed to create semaphore");
    return ESP_ERR_NO_MEM;
  }

  s_conn_count = 0;
  s_is_initialized = true;
  ESP_LOGI(TAG, "BLE initialized");
  return ESP_OK;
}

esp_err_t bluetooth_service_start(void) {
  if (!s_is_initialized) {
    ESP_LOGE(TAG, "BLE not initialized");
    return ESP_FAIL;
  }
  if (s_is_running) {
    ESP_LOGW(TAG, "BLE already running");
    return ESP_OK;
  }

  nimble_port_freertos_init(host_task);
  s_is_running = true;

  ESP_LOGI(TAG, "Waiting for BLE host synchronization");
  if (xSemaphoreTake(s_hs_synced_sem, pdMS_TO_TICKS(BLE_SYNC_TIMEOUT_MS)) == pdFALSE) {
    ESP_LOGE(TAG, "Timeout syncing BLE host");
    bluetooth_service_stop();
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "BLE started");
  return ESP_OK;
}

esp_err_t bluetooth_service_stop(void) {
  if (!s_is_running) {
    ESP_LOGW(TAG, "BLE not running");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Stopping BLE host task");
  int rc = nimble_port_stop();
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to stop NimBLE port: %d", rc);
    return ESP_FAIL;
  }

  s_is_running = false;
  return ESP_OK;
}

esp_err_t bluetooth_service_deinit(void) {
  if (s_is_running) {
    bluetooth_service_stop();
  }
  if (!s_is_initialized) {
    ESP_LOGW(TAG, "BLE not initialized");
    return ESP_OK;
  }

  nimble_port_deinit();

  if (s_hs_synced_sem != NULL) {
    vSemaphoreDelete(s_hs_synced_sem);
    s_hs_synced_sem = NULL;
  }

  s_is_initialized = false;
  s_is_running = false;
  ESP_LOGI(TAG, "BLE deinitialized");
  return ESP_OK;
}

bool bluetooth_service_is_initialized(void) {
  return s_is_initialized;
}

bool bluetooth_service_is_running(void) {
  return s_is_running;
}

void bluetooth_service_disconnect_all(void) {
  if (!s_is_running) {
    return;
  }
  for (int i = 0; i < s_conn_count; i++) {
    ble_gap_terminate(s_conn_handles[i], BLE_ERR_REM_USER_CONN_TERM);
  }
}

int bluetooth_service_get_connected_count(void) {
  return s_conn_count;
}

void bluetooth_service_get_mac(uint8_t *out_mac) {
  if (s_is_initialized) {
    ble_hs_id_copy_addr(s_own_addr_type, out_mac, NULL);
  } else {
    memset(out_mac, 0, BLE_MAC_LEN);
  }
}

esp_err_t bluetooth_service_set_random_mac(void) {
  if (!s_is_initialized) {
    ESP_LOGE(TAG, "BLE not initialized");
    return ESP_FAIL;
  }

  bluetooth_service_stop_advertising();

  uint8_t addr[BLE_MAC_LEN];
  esp_fill_random(addr, BLE_MAC_LEN);
  // Random Static Address: two MSBs of last octet must be 11
  addr[5] |= BLE_RANDOM_ADDR_MSB;

  int rc = ble_hs_id_set_rnd(addr);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to set random address: %d", rc);
    return ESP_FAIL;
  }

  s_own_addr_type = BLE_OWN_ADDR_RANDOM;
  ESP_LOGI(TAG,
           "Random MAC set: %02x:%02x:%02x:%02x:%02x:%02x",
           addr[5],
           addr[4],
           addr[3],
           addr[2],
           addr[1],
           addr[0]);

  return ESP_OK;
}

esp_err_t bluetooth_service_connect(const uint8_t *addr,
                                    uint8_t addr_type,
                                    int (*cb)(struct ble_gap_event *event, void *arg)) {
  if (!s_is_running) {
    ESP_LOGE(TAG, "BLE not running");
    return ESP_FAIL;
  }

  bluetooth_service_stop_advertising();

  ble_addr_t target_addr;
  memcpy(target_addr.val, addr, BLE_MAC_LEN);
  target_addr.type = addr_type;

  struct ble_gap_conn_params conn_params;
  memset(&conn_params, 0, sizeof(conn_params));
  conn_params.scan_itvl = BLE_SCAN_ITVL;
  conn_params.scan_window = BLE_SCAN_WINDOW;
  conn_params.itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN;
  conn_params.itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX;
  conn_params.latency = 0;
  conn_params.supervision_timeout = BLE_SUPERVISION_TIMEOUT;
  conn_params.min_ce_len = BLE_MIN_CE_LEN;
  conn_params.max_ce_len = BLE_MAX_CE_LEN;

  int rc = ble_gap_connect(
      s_own_addr_type, &target_addr, BLE_CONNECT_TIMEOUT_MS, &conn_params, cb, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error initiating connection: %d", rc);
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t bluetooth_service_start_advertising(void) {
  if (!s_is_initialized) {
    ESP_LOGE(TAG, "BLE not initialized");
    return ESP_FAIL;
  }

  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;

  memset(&fields, 0, sizeof(fields));
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  fields.uuids16 = (ble_uuid16_t[]){BLE_UUID16_INIT(BLE_SVC_DIS_UUID16)};
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  const char *device_name = ble_svc_gap_device_name();
  fields.name = (uint8_t *)device_name;
  fields.name_len = strlen(device_name);
  fields.name_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error setting advertisement fields: rc=%d", rc);
    return ESP_FAIL;
  }

  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error starting advertisement: rc=%d", rc);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Advertising started");
  return ESP_OK;
}

esp_err_t bluetooth_service_stop_advertising(void) {
  if (!s_is_initialized) {
    ESP_LOGE(TAG, "BLE not initialized");
    return ESP_FAIL;
  }
  if (ble_gap_adv_active()) {
    return ble_gap_adv_stop();
  }
  return ESP_OK;
}

esp_err_t bluetooth_service_set_max_power(void) {
  esp_err_t err;

  err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set ADV TX power: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "ADV TX power set to max (P9)");
  }

  err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set default TX power: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Default TX power set to max (P9)");
  }

  return ESP_OK;
}

uint8_t bluetooth_service_get_own_addr_type(void) {
  return s_own_addr_type;
}

esp_err_t bluetooth_service_save_announce_config(const char *name, uint8_t max_conn) {
  if (name == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return ESP_ERR_NO_MEM;
  }

  cJSON_AddStringToObject(root, "ssid", name);
  cJSON_AddNumberToObject(root, "max_conn", max_conn);

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = storage_write_string(BLE_ANNOUNCE_CONFIG_PATH, json_string);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "BLE config saved to: %s", BLE_ANNOUNCE_CONFIG_PATH);
  } else {
    ESP_LOGE(TAG, "Error writing BLE config: %s", esp_err_to_name(err));
  }

  free(json_string);
  cJSON_Delete(root);
  return err;
}

esp_err_t bluetooth_service_load_spam_list(char ***out_list, size_t *out_count) {
  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char *)storage_assets_load_file(BLE_SPAM_LIST_FILE, &size);

  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to load spam list");
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(buffer);
  if (root == NULL) {
    free(buffer);
    return ESP_FAIL;
  }

  cJSON *spam_array = cJSON_GetObjectItem(root, "spam_ble_announce");
  if (!cJSON_IsArray(spam_array)) {
    cJSON_Delete(root);
    free(buffer);
    return ESP_FAIL;
  }

  int array_size = cJSON_GetArraySize(spam_array);
  *out_list = malloc(array_size * sizeof(char *));
  if (*out_list == NULL) {
    cJSON_Delete(root);
    free(buffer);
    return ESP_ERR_NO_MEM;
  }

  *out_count = 0;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, spam_array) {
    if (cJSON_IsString(item)) {
      (*out_list)[*out_count] = strdup(item->valuestring);
      (*out_count)++;
    }
  }

  cJSON_Delete(root);
  free(buffer);
  return ESP_OK;
}

esp_err_t bluetooth_service_save_spam_list(const char *const *list, size_t count) {
  cJSON *root = cJSON_CreateObject();
  cJSON *array = cJSON_CreateStringArray(list, count);

  cJSON_AddItemToObject(root, "spam_ble_announce", array);

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = storage_write_string(BLE_SPAM_LIST_PATH, json_string);

  free(json_string);
  cJSON_Delete(root);
  return err;
}

void bluetooth_service_free_spam_list(char **list, size_t count) {
  if (list != NULL) {
    for (size_t i = 0; i < count; i++) {
      free(list[i]);
    }
    free(list);
  }
}

void bluetooth_service_scan(uint32_t duration_ms) {
  if (!s_is_initialized) {
    ESP_LOGE(TAG, "BLE not initialized");
    return;
  }

  if (s_scan_sem == NULL) {
    s_scan_sem = xSemaphoreCreateBinary();
  }

  bluetooth_service_stop_advertising();

  s_scan_count = 0;
  memset(s_scan_results, 0, sizeof(s_scan_results));

  struct ble_gap_disc_params disc_params;
  memset(&disc_params, 0, sizeof(disc_params));
  disc_params.filter_duplicates = 0;
  disc_params.passive = 0;
  disc_params.itvl = 0;
  disc_params.window = 0;
  disc_params.filter_policy = 0;
  disc_params.limited = 0;

  ESP_LOGI(TAG, "Starting BLE scan for %lu ms", duration_ms);

  int rc = ble_gap_disc(s_own_addr_type, duration_ms, &disc_params, gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error initiating GAP discovery: %d", rc);
    return;
  }

  xSemaphoreTake(s_scan_sem, portMAX_DELAY);
  bluetooth_service_start_advertising();
}

esp_err_t bluetooth_service_start_sniffer(bluetooth_service_sniffer_cb_t cb) {
  if (!s_is_initialized || !s_is_running) {
    return ESP_FAIL;
  }

  bluetooth_service_stop_advertising();
  s_sniffer_cb = cb;

  struct ble_gap_disc_params disc_params;
  memset(&disc_params, 0, sizeof(disc_params));
  disc_params.filter_duplicates = 0;
  disc_params.passive = 1;
  disc_params.itvl = 0;
  disc_params.window = 0;

  ESP_LOGI(TAG, "Starting BLE sniffer");
  int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &disc_params, gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Sniffer start failed: %d", rc);
    s_sniffer_cb = NULL;
    return ESP_FAIL;
  }
  return ESP_OK;
}

void bluetooth_service_stop_sniffer(void) {
  if (ble_gap_disc_active()) {
    ble_gap_disc_cancel();
  }
  s_sniffer_cb = NULL;
  ESP_LOGI(TAG, "Sniffer stopped");
}

esp_err_t bluetooth_service_start_tracker(const uint8_t *addr, bluetooth_service_tracker_cb_t cb) {
  if (!s_is_initialized || !s_is_running) {
    return ESP_FAIL;
  }

  bluetooth_service_stop_advertising();
  s_tracker_cb = cb;
  memcpy(s_tracker_target_addr, addr, BLE_MAC_LEN);

  struct ble_gap_disc_params disc_params;
  memset(&disc_params, 0, sizeof(disc_params));
  disc_params.filter_duplicates = 0;
  disc_params.passive = 1;
  disc_params.itvl = 0;
  disc_params.window = 0;

  ESP_LOGI(TAG, "Starting RSSI tracker");
  int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &disc_params, gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Tracker start failed: %d", rc);
    s_tracker_cb = NULL;
    return ESP_FAIL;
  }
  return ESP_OK;
}

void bluetooth_service_stop_tracker(void) {
  if (ble_gap_disc_active()) {
    ble_gap_disc_cancel();
  }
  s_tracker_cb = NULL;
  ESP_LOGI(TAG, "Tracker stopped");
}

uint16_t bluetooth_service_get_scan_count(void) {
  return s_scan_count;
}

bluetooth_service_scan_result_t *bluetooth_service_get_scan_result(uint16_t index) {
  if (index < s_scan_count) {
    return &s_scan_results[index];
  }
  return NULL;
}

// Static function implementations

static void on_reset(int reason) {
  ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

static void on_sync(void) {
  ESP_LOGI(TAG, "BLE synced");
  int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error inferring address type: %d", rc);
    return;
  }
  if (s_hs_synced_sem != NULL) {
    xSemaphoreGive(s_hs_synced_sem);
  }
}

static void host_task(void *param) {
  ESP_LOGI(TAG, "BLE host task started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static int gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      ESP_LOGI(TAG,
               "Device connected, status=%d conn_handle=%d",
               event->connect.status,
               event->connect.conn_handle);
      if (event->connect.status == 0) {
        if (s_conn_count < MAX_BLE_CONNECTIONS) {
          s_conn_handles[s_conn_count++] = event->connect.conn_handle;
        }
      } else {
        bluetooth_service_start_advertising();
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "Device disconnected, reason=%d", event->disconnect.reason);
      for (int i = 0; i < s_conn_count; i++) {
        if (s_conn_handles[i] == event->disconnect.conn.conn_handle) {
          for (int j = i; j < s_conn_count - 1; j++) {
            s_conn_handles[j] = s_conn_handles[j + 1];
          }
          s_conn_count--;
          break;
        }
      }
      bluetooth_service_start_advertising();
      break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      ESP_LOGI(TAG, "Advertising complete");
      bluetooth_service_start_advertising();
      break;

    case BLE_GAP_EVENT_DISC: {
      if (s_sniffer_cb != NULL) {
        s_sniffer_cb(event->disc.addr.val,
                     event->disc.addr.type,
                     event->disc.rssi,
                     event->disc.data,
                     event->disc.length_data);
        return 0;
      }

      if (s_tracker_cb != NULL) {
        if (memcmp(event->disc.addr.val, s_tracker_target_addr, BLE_MAC_LEN) == 0) {
          s_tracker_cb(event->disc.rssi);
        }
        return 0;
      }

      struct ble_hs_adv_fields fields;
      int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
      if (rc != 0) {
        return 0;
      }

      bool is_found = false;
      for (int i = 0; i < s_scan_count; i++) {
        if (memcmp(s_scan_results[i].addr, event->disc.addr.val, BLE_MAC_LEN) == 0) {
          is_found = true;
          s_scan_results[i].rssi = event->disc.rssi;

          if (fields.name != NULL && s_scan_results[i].name[0] == 0) {
            int name_len = fields.name_len;
            if (name_len > BLE_NAME_MAX_LEN) {
              name_len = BLE_NAME_MAX_LEN;
            }
            memcpy(s_scan_results[i].name, fields.name, name_len);
            s_scan_results[i].name[name_len] = '\0';
          }

          if (fields.num_uuids16 > 0) {
            if (s_scan_results[i].uuids[0] == 0) {
              char *ptr = s_scan_results[i].uuids;
              size_t remaining = sizeof(s_scan_results[i].uuids);
              for (int u = 0; u < fields.num_uuids16; u++) {
                int written = snprintf(ptr, remaining, "0x%04x ", fields.uuids16[u].value);
                if (written > 0 && (size_t)written < remaining) {
                  ptr += written;
                  remaining -= written;
                }
              }
            }
          } else if (fields.num_uuids128 > 0 && s_scan_results[i].uuids[0] == 0) {
            const uint8_t *u128 = fields.uuids128[0].value;
            snprintf(s_scan_results[i].uuids,
                     sizeof(s_scan_results[i].uuids),
                     "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
                     "%02x%02x-%02x%02x%02x%02x%02x%02x",
                     u128[15],
                     u128[14],
                     u128[13],
                     u128[12],
                     u128[11],
                     u128[10],
                     u128[9],
                     u128[8],
                     u128[7],
                     u128[6],
                     u128[5],
                     u128[4],
                     u128[3],
                     u128[2],
                     u128[1],
                     u128[0]);
          }
          break;
        }
      }

      if (!is_found && s_scan_count < BLE_SCAN_LIST_SIZE) {
        memcpy(s_scan_results[s_scan_count].addr, event->disc.addr.val, BLE_MAC_LEN);
        s_scan_results[s_scan_count].addr_type = event->disc.addr.type;
        s_scan_results[s_scan_count].rssi = event->disc.rssi;
        s_scan_results[s_scan_count].uuids[0] = '\0';

        if (fields.num_uuids16 > 0) {
          char *ptr = s_scan_results[s_scan_count].uuids;
          size_t remaining = sizeof(s_scan_results[s_scan_count].uuids);
          for (int u = 0; u < fields.num_uuids16; u++) {
            int written = snprintf(ptr, remaining, "0x%04x ", fields.uuids16[u].value);
            if (written > 0 && (size_t)written < remaining) {
              ptr += written;
              remaining -= written;
            }
          }
        } else if (fields.num_uuids128 > 0) {
          const uint8_t *u128 = fields.uuids128[0].value;
          snprintf(s_scan_results[s_scan_count].uuids,
                   sizeof(s_scan_results[s_scan_count].uuids),
                   "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
                   "%02x%02x-%02x%02x%02x%02x%02x%02x",
                   u128[15],
                   u128[14],
                   u128[13],
                   u128[12],
                   u128[11],
                   u128[10],
                   u128[9],
                   u128[8],
                   u128[7],
                   u128[6],
                   u128[5],
                   u128[4],
                   u128[3],
                   u128[2],
                   u128[1],
                   u128[0]);
        }

        if (fields.name != NULL) {
          int name_len = fields.name_len;
          if (name_len > BLE_NAME_MAX_LEN) {
            name_len = BLE_NAME_MAX_LEN;
          }
          memcpy(s_scan_results[s_scan_count].name, fields.name, name_len);
          s_scan_results[s_scan_count].name[name_len] = '\0';
        } else {
          s_scan_results[s_scan_count].name[0] = '\0';
        }
        s_scan_count++;
      }
      return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
      ESP_LOGI(TAG, "Scan complete, reason=%d", event->disc_complete.reason);
      if (s_scan_sem != NULL) {
        xSemaphoreGive(s_scan_sem);
      }
      return 0;

    default:
      break;
  }
  return 0;
}

static void load_announce_config(char *out_name, uint8_t *out_max_conn) {
  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char *)storage_assets_load_file(BLE_ANNOUNCE_CONFIG_FILE, &size);

  if (buffer != NULL) {
    cJSON *root = cJSON_Parse(buffer);
    if (root != NULL) {
      cJSON *j_name = cJSON_GetObjectItem(root, "ssid");
      cJSON *j_conn = cJSON_GetObjectItem(root, "max_conn");

      if (cJSON_IsString(j_name) && strlen(j_name->valuestring) > 0) {
        strncpy(out_name, j_name->valuestring, BLE_NAME_MAX_LEN);
        out_name[BLE_NAME_MAX_LEN] = '\0';
      }
      if (cJSON_IsNumber(j_conn)) {
        *out_max_conn = (uint8_t)j_conn->valueint;
      }

      cJSON_Delete(root);
      ESP_LOGI(TAG, "BLE config loaded");
    }
    free(buffer);
  } else {
    ESP_LOGW(TAG, "BLE config file not found, using defaults");
  }
}
