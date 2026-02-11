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



#include "bluetooth_service.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "assert.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_stop.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/dis/ble_svc_dis.h"

#include "storage_write.h"
#include "storage_assets.h"
#include "cJSON.h"

#define BLE_ANNOUNCE_CONFIG_FILE "config/bluetooth/ble_announce.conf"
#define BLE_ANNOUNCE_CONFIG_PATH "/assets/" BLE_ANNOUNCE_CONFIG_FILE
#define BLE_SPAM_LIST_FILE "config/bluetooth/beacon_list.conf"
#define BLE_SPAM_LIST_PATH "/assets/" BLE_SPAM_LIST_FILE

static const char *TAG = "BLE_SERVICE";

static uint8_t own_addr_type;
static bool ble_initialized = false;
static bool ble_running = false;
static SemaphoreHandle_t ble_hs_synced_sem = NULL; 

#define MAX_BLE_CONNECTIONS 8
static uint16_t connection_handles[MAX_BLE_CONNECTIONS];
static int connection_count = 0;

static ble_sniffer_cb_t sniffer_cb = NULL;

static ble_tracker_cb_t tracker_cb = NULL;
static uint8_t tracker_target_addr[6];

static ble_scan_result_t scan_results[BLE_SCAN_LIST_SIZE];
static uint16_t scan_count = 0;
static SemaphoreHandle_t scan_sem = NULL;

static void bluetooth_service_on_reset(int reason);
static void bluetooth_service_on_sync(void);
static void bluetooth_service_host_task(void *param);
static int bluetooth_service_gap_event(struct ble_gap_event *event, void *arg);
static void bluetooth_load_announce_config(char* name, uint8_t* max_conn);


static int bluetooth_service_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      ESP_LOGI(TAG, "Device connected! status=%d conn_handle=%d",
               event->connect.status, event->connect.conn_handle);
      if (event->connect.status == 0) {
        if (connection_count < MAX_BLE_CONNECTIONS) {
          connection_handles[connection_count++] = event->connect.conn_handle;
        }
      } else {
        bluetooth_service_start_advertising();
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "Device disconnected! reason=%d", event->disconnect.reason);
      for (int i = 0; i < connection_count; i++) {
        if (connection_handles[i] == event->disconnect.conn.conn_handle) {
          for (int j = i; j < connection_count - 1; j++) {
            connection_handles[j] = connection_handles[j+1];
          }
          connection_count--;
          break;
        }
      }
      bluetooth_service_start_advertising();
      break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      ESP_LOGI(TAG, "Advertising complete!");
      bluetooth_service_start_advertising();
      break;
    case BLE_GAP_EVENT_DISC: {
      if (sniffer_cb) {
        sniffer_cb(event->disc.addr.val, event->disc.addr.type, event->disc.rssi, event->disc.data, event->disc.length_data);
        return 0;
      }

      if (tracker_cb) {
        if (memcmp(event->disc.addr.val, tracker_target_addr, 6) == 0) {
          tracker_cb(event->disc.rssi);
        }
        return 0;
      }

      struct ble_hs_adv_fields fields;            int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);      if (rc != 0) {
        return 0;
      }

      bool found = false;
      for (int i = 0; i < scan_count; i++) {
        if (memcmp(scan_results[i].addr, event->disc.addr.val, 6) == 0) {
          found = true;
          scan_results[i].rssi = event->disc.rssi;
          if (fields.name != NULL && scan_results[i].name[0] == 0) {
            int name_len = fields.name_len;
            if (name_len > 31) name_len = 31;
            memcpy(scan_results[i].name, fields.name, name_len);
            scan_results[i].name[name_len] = '\0';
          }

          if (fields.num_uuids16 > 0) {
            char *ptr = scan_results[i].uuids;
            size_t remaining = sizeof(scan_results[i].uuids);

            if (scan_results[i].uuids[0] != 0) {
            } else {
              for (int u = 0; u < fields.num_uuids16; u++) {
                int written = snprintf(ptr, remaining, "0x%04x ", fields.uuids16[u].value);
                if (written > 0 && written < remaining) {
                  ptr += written;
                  remaining -= written;
                }
              }
            }
          }
          else if (fields.num_uuids128 > 0 && scan_results[i].uuids[0] == 0) {
            const uint8_t *u128 = fields.uuids128[0].value;
            snprintf(scan_results[i].uuids, sizeof(scan_results[i].uuids), 
                     "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                     u128[15], u128[14], u128[13], u128[12], u128[11], u128[10], u128[9], u128[8],
                     u128[7], u128[6], u128[5], u128[4], u128[3], u128[2], u128[1], u128[0]);
          }
          break;
        }
      }

      if (!found && scan_count < BLE_SCAN_LIST_SIZE) {
        memcpy(scan_results[scan_count].addr, event->disc.addr.val, 6);
        scan_results[scan_count].addr_type = event->disc.addr.type;
        scan_results[scan_count].rssi = event->disc.rssi;
        scan_results[scan_count].uuids[0] = '\0';

        if (fields.num_uuids16 > 0) {
          char *ptr = scan_results[scan_count].uuids;
          size_t remaining = sizeof(scan_results[scan_count].uuids);
          for (int u = 0; u < fields.num_uuids16; u++) {
            int written = snprintf(ptr, remaining, "0x%04x ", fields.uuids16[u].value);
            if (written > 0 && written < remaining) {
              ptr += written;
              remaining -= written;
            }
          }
        } else if (fields.num_uuids128 > 0) {
          const uint8_t *u128 = fields.uuids128[0].value;
          snprintf(scan_results[scan_count].uuids, sizeof(scan_results[scan_count].uuids), 
                   "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                   u128[15], u128[14], u128[13], u128[12], u128[11], u128[10], u128[9], u128[8],
                   u128[7], u128[6], u128[5], u128[4], u128[3], u128[2], u128[1], u128[0]);
        }

        if (fields.name != NULL) {
          int name_len = fields.name_len;
          if (name_len > 31) name_len = 31;
          memcpy(scan_results[scan_count].name, fields.name, name_len);
          scan_results[scan_count].name[name_len] = '\0';
        } else {
          scan_results[scan_count].name[0] = '\0';
        }
        scan_count++;
      }      return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
      ESP_LOGI(TAG, "Scan complete reason=%d", event->disc_complete.reason);
      if (scan_sem) {
        xSemaphoreGive(scan_sem);
      }
      return 0;
    default:
      break;
  }
  return 0;
}

esp_err_t bluetooth_service_connect(const uint8_t *addr, uint8_t addr_type, int (*cb)(struct ble_gap_event *event, void *arg)) {
  if (!ble_running) {
    ESP_LOGE(TAG, "BLE not running");
    return ESP_FAIL;
  }

  bluetooth_service_stop_advertising();

  ble_addr_t target_addr;
  memcpy(target_addr.val, addr, 6);
  target_addr.type = addr_type;

  struct ble_gap_conn_params conn_params;
  memset(&conn_params, 0, sizeof(conn_params));
  conn_params.scan_itvl = 0x0010;
  conn_params.scan_window = 0x0010;
  conn_params.itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN;
  conn_params.itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX;
  conn_params.latency = 0;
  conn_params.supervision_timeout = 0x0100;
  conn_params.min_ce_len = 0x0010;
  conn_params.max_ce_len = 0x0300;

  int rc = ble_gap_connect(own_addr_type, &target_addr, 30000, &conn_params, cb, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error initiating connection: %d", rc);
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t bluetooth_service_start_advertising(void) {
  if (!ble_initialized) {
    ESP_LOGE(TAG, "BLE not initialized. Cannot advertise.");
    return ESP_FAIL;
  }

  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;
  int rc;

  memset(&fields, 0, sizeof(fields));
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  fields.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(BLE_SVC_DIS_UUID16) };
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  const char* device_name = ble_svc_gap_device_name();
  fields.name = (uint8_t *)device_name;
  fields.name_len = strlen(device_name);
  fields.name_is_complete = 1;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error setting advertisement fields; rc=%d", rc);
    return ESP_FAIL;
  }

  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, bluetooth_service_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error starting advertisement; rc=%d", rc);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Default advertising started successfully");
  return ESP_OK;
}

esp_err_t bluetooth_service_stop_advertising(void) {
  if (!ble_initialized) {
    ESP_LOGE(TAG, "BLE not initialized.");
    return ESP_FAIL;
  }
  if (ble_gap_adv_active()) {
    return ble_gap_adv_stop();
  }
  return ESP_OK;
}

void bluetooth_service_on_reset(int reason) {
  ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

void bluetooth_service_on_sync(void) {
  ESP_LOGI(TAG, "BLE synced");
  int rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error inferring address type: %d", rc);
    return;
  }
  if (ble_hs_synced_sem != NULL) {
    xSemaphoreGive(ble_hs_synced_sem);
  }
}

void bluetooth_service_host_task(void *param) {
  ESP_LOGI(TAG, "BLE Host Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

esp_err_t bluetooth_service_init(void) {
  if (ble_initialized) {
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

  ble_hs_cfg.reset_cb = bluetooth_service_on_reset;
  ble_hs_cfg.sync_cb = bluetooth_service_on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  char device_name[32] = "Darth Maul";
  uint8_t max_conn = 4; 
  bluetooth_load_announce_config(device_name, &max_conn);

  ret = ble_svc_gap_device_name_set(device_name);
  assert(ret == 0);

  ble_hs_synced_sem = xSemaphoreCreateBinary();
  if (ble_hs_synced_sem == NULL) {
    ESP_LOGE(TAG, "Failed to create semaphore");
    return ESP_ERR_NO_MEM;
  }

  connection_count = 0;
  ble_initialized = true;
  ESP_LOGI(TAG, "BLE initialized (resources allocated). Call start to run.");
  return ESP_OK;
}

esp_err_t bluetooth_service_start(void) {
  if (!ble_initialized) {
    ESP_LOGE(TAG, "BLE not initialized. Call init first.");
    return ESP_FAIL;
  }
  if (ble_running) {
    ESP_LOGW(TAG, "BLE already running.");
    return ESP_OK;
  }

  nimble_port_freertos_init(bluetooth_service_host_task);
  ble_running = true;

  ESP_LOGI(TAG, "Waiting for BLE Host synchronization...");
  if (xSemaphoreTake(ble_hs_synced_sem, pdMS_TO_TICKS(10000)) == pdFALSE) {
    ESP_LOGE(TAG, "Timeout syncing BLE Host");
    bluetooth_service_stop(); 
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "BLE started successfully");
  return ESP_OK;
}

esp_err_t bluetooth_service_stop(void) {
  if (!ble_running) {
    ESP_LOGW(TAG, "BLE not running");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Stopping BLE host task");
  int rc = nimble_port_stop();
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to stop NimBLE port: %d", rc);
    return ESP_FAIL;
  }

  ble_running = false;
  return ESP_OK;
}

esp_err_t bluetooth_service_deinit(void) {
  if (ble_running) {
    bluetooth_service_stop();
  }
  if (!ble_initialized) {
    ESP_LOGW(TAG, "BLE not initialized");
    return ESP_OK;
  }

  nimble_port_deinit();

  if (ble_hs_synced_sem != NULL) {
    vSemaphoreDelete(ble_hs_synced_sem);
    ble_hs_synced_sem = NULL;
  }

  ble_initialized = false;
  ESP_LOGI(TAG, "BLE deinitialized");
  return ESP_OK;
}

esp_err_t bluetooth_service_set_max_power(void) {
  esp_err_t err;

  err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set TX power for ADV: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "ADV TX power set to MAX (P9)");
  }

  err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set Default TX power: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Default TX power set to MAX (P9)");
  }

  return ESP_OK; 
}

uint8_t bluetooth_service_get_own_addr_type(void) {
  return own_addr_type;
}


esp_err_t bluetooth_save_announce_config(const char *name, uint8_t max_conn) {
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
    ESP_LOGI(TAG, "BLE config saved successfully to: %s", BLE_ANNOUNCE_CONFIG_PATH);
  } else {
    ESP_LOGE(TAG, "Error writing BLE config file: %s", esp_err_to_name(err));
  }

  free(json_string);
  cJSON_Delete(root);
  return err;
}

static void bluetooth_load_announce_config(char* name, uint8_t* max_conn) {
  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char*)storage_assets_load_file(BLE_ANNOUNCE_CONFIG_FILE, &size);

  if (buffer != NULL) {
    cJSON *root = cJSON_Parse(buffer);
    if (root) {
      cJSON *j_name = cJSON_GetObjectItem(root, "ssid");
      cJSON *j_conn = cJSON_GetObjectItem(root, "max_conn");

      if (cJSON_IsString(j_name) && (strlen(j_name->valuestring) > 0)) {
        strncpy(name, j_name->valuestring, 31);
        name[31] = '\0';
      }
      if (cJSON_IsNumber(j_conn)){
        *max_conn = (uint8_t)j_conn->valueint;
      }

      cJSON_Delete(root);
      ESP_LOGI(TAG, "BLE configs loaded successfully.");
    }
    free(buffer);
  } else {
    ESP_LOGW(TAG, "BLE config file not found. Using defaults.");
  }
}

esp_err_t bluetooth_load_spam_list(char ***list, size_t *count) {
  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }

  size_t size = 0;
  char *buffer = (char*)storage_assets_load_file(BLE_SPAM_LIST_FILE, &size);

  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to load spam list.");
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(buffer);
  if (!root) {
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
  *list = malloc(array_size * sizeof(char*));
  if (*list == NULL) {
    cJSON_Delete(root);
    free(buffer);
    return ESP_ERR_NO_MEM;
  }

  *count = 0;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, spam_array) {
    if (cJSON_IsString(item)) {
      (*list)[*count] = strdup(item->valuestring);
      (*count)++;
    }
  }

  cJSON_Delete(root);
  free(buffer);
  return ESP_OK;
}

esp_err_t bluetooth_save_spam_list(const char * const *list, size_t count) {
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

void bluetooth_free_spam_list(char **list, size_t count) {
  if (list != NULL) {
    for (size_t i = 0; i < count; i++) {
      free(list[i]);
    }
    free(list);
  }
}

void bluetooth_service_scan(uint32_t duration_ms) {
  if (!ble_initialized) {
    ESP_LOGE(TAG, "BLE not initialized");
    return;
  }

  if (scan_sem == NULL) {
    scan_sem = xSemaphoreCreateBinary();
  }

  bluetooth_service_stop_advertising();

  scan_count = 0;
  memset(scan_results, 0, sizeof(scan_results));

  struct ble_gap_disc_params disc_params;
  memset(&disc_params, 0, sizeof(disc_params));
  disc_params.filter_duplicates = 0;
  disc_params.passive = 0; 
  disc_params.itvl = 0; 
  disc_params.window = 0; 
  disc_params.filter_policy = 0;
  disc_params.limited = 0;

  ESP_LOGI(TAG, "Starting BLE scan for %lu ms...", duration_ms);

  int rc = ble_gap_disc(own_addr_type, duration_ms, &disc_params, bluetooth_service_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error initiating GAP discovery: %d", rc);
    return;
  }

  xSemaphoreTake(scan_sem, portMAX_DELAY); 

  bluetooth_service_start_advertising();
}

esp_err_t bluetooth_service_start_sniffer(ble_sniffer_cb_t cb) {
  if (!ble_initialized || !ble_running) return ESP_FAIL;

  bluetooth_service_stop_advertising();
  sniffer_cb = cb;

  struct ble_gap_disc_params disc_params;
  memset(&disc_params, 0, sizeof(disc_params));
  disc_params.filter_duplicates = 0; // Capture everything
  disc_params.passive = 1; // Passive scan (don't request more data, just listen)
  disc_params.itvl = 0; 
  disc_params.window = 0; 

  ESP_LOGI(TAG, "Starting Raw Sniffer...");
  int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, bluetooth_service_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Sniffer start failed: %d", rc);
    sniffer_cb = NULL;
    return ESP_FAIL;
  }
  return ESP_OK;
}

void bluetooth_service_stop_sniffer(void) {
  if (ble_gap_disc_active()) {
    ble_gap_disc_cancel();
  }
  sniffer_cb = NULL;
  ESP_LOGI(TAG, "Sniffer stopped.");
}

esp_err_t bluetooth_service_start_tracker(const uint8_t *addr, ble_tracker_cb_t cb) {
  if (!ble_initialized || !ble_running) return ESP_FAIL;

  bluetooth_service_stop_advertising();
  tracker_cb = cb;
  memcpy(tracker_target_addr, addr, 6);

  struct ble_gap_disc_params disc_params;
  memset(&disc_params, 0, sizeof(disc_params));
  disc_params.filter_duplicates = 0;
  disc_params.passive = 1;
  disc_params.itvl = 0;
  disc_params.window = 0;

  ESP_LOGI(TAG, "Starting RSSI Tracker...");
  int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, bluetooth_service_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Tracker start failed: %d", rc);
    tracker_cb = NULL;
    return ESP_FAIL;
  }
  return ESP_OK;
}

void bluetooth_service_stop_tracker(void) {
  if (ble_gap_disc_active()) {
    ble_gap_disc_cancel();
  }
  tracker_cb = NULL;
  ESP_LOGI(TAG, "Tracker stopped.");
}

uint16_t bluetooth_service_get_scan_count(void) {
  return scan_count;
}

ble_scan_result_t* bluetooth_service_get_scan_result(uint16_t index) {
  if (index < scan_count) {
    return &scan_results[index];
  }
  return NULL;
}

bool bluetooth_service_is_initialized(void) {
  return ble_initialized;
}

bool bluetooth_service_is_running(void) {
  return ble_running;
}

void bluetooth_service_disconnect_all(void) {
  if (!ble_running) return;
  for (int i = 0; i < connection_count; i++) {
    ble_gap_terminate(connection_handles[i], BLE_ERR_REM_USER_CONN_TERM);
  }
}

int bluetooth_service_get_connected_count(void) {
  return connection_count;
}
void bluetooth_service_get_mac(uint8_t *mac) {
  if (ble_initialized) {
    ble_hs_id_copy_addr(own_addr_type, mac, NULL);
  } else {
    memset(mac, 0, 6);
  }
}

esp_err_t bluetooth_service_set_random_mac(void) {
  if (!ble_initialized) {
    ESP_LOGE(TAG, "BLE not initialized");
    return ESP_FAIL;
  }

  bluetooth_service_stop_advertising();

  uint8_t addr[6];
  esp_fill_random(addr, 6);
  // Random Static Address (MSB 11)
  addr[5] |= 0xC0;

  int rc = ble_hs_id_set_rnd(addr);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to set random address: %d", rc);
    return ESP_FAIL;
  }

  own_addr_type = BLE_OWN_ADDR_RANDOM;
  ESP_LOGI(TAG, "Random MAC set: %02x:%02x:%02x:%02x:%02x:%02x",
           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

  return ESP_OK;
}
