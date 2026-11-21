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

#include <stdatomic.h>
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
#include "gatt_explorer.h"
#include "storage_assets.h"
#include "storage_write.h"
#include "ble_hid_keyboard.h"
#include "sys_prio.h"

static const char *TAG = "BLE_SERVICE";

#define BLE_ANNOUNCE_CONFIG_FILE "config/bluetooth/ble_announce.conf"
#define BLE_ANNOUNCE_CONFIG_PATH "/assets/" BLE_ANNOUNCE_CONFIG_FILE
#define BLE_SPAM_LIST_FILE       "config/bluetooth/beacon_list.conf"
#define BLE_SPAM_LIST_PATH       "/assets/" BLE_SPAM_LIST_FILE
#define MAX_BLE_CONNECTIONS      8
#define BLE_SYNC_TIMEOUT_MS      2000
#define BLE_STOP_TIMEOUT_MS      3000
#define BLE_HOST_EXIT_TIMEOUT_MS 500
#define BLE_STOP_STACK_SIZE      3072
#define BLE_SCAN_MAX_MS          30000
#define BLE_SCAN_MARGIN_MS       2000
#define BLE_CONNECT_TIMEOUT_MS   30000
#define BLE_NAME_MAX_LEN         31
#define BLE_MAC_LEN              6
#define BLE_RANDOM_ADDR_MSB      0xC0
#define BLE_SCAN_ITVL            0x0010
#define BLE_SCAN_WINDOW          0x0010
#define BLE_SUPERVISION_TIMEOUT  0x0100
#define BLE_MIN_CE_LEN           0x0010
#define BLE_MAX_CE_LEN           0x0300
#define DEFAULT_MAX_CONN         CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define BLE_ADVERTISED_NAME_MAX  29

static uint8_t s_own_addr_type;
static atomic_bool s_is_initialized;
static atomic_bool s_is_running;
static atomic_bool s_is_synced;
static atomic_bool s_has_fault;
static atomic_bool s_is_stopping;
static atomic_bool s_is_scanning;
static atomic_bool s_adv_requested;
static atomic_flag s_lifecycle_busy = ATOMIC_FLAG_INIT;
static atomic_bool s_has_host;
static atomic_bool s_has_controller;
static TaskHandle_t s_host_task;
static TaskHandle_t s_stop_task;
static bool s_has_stop_ack;
static SemaphoreHandle_t s_host_exited_sem;
static SemaphoreHandle_t s_stop_done_sem;
static atomic_int s_sync_error;
static atomic_int s_stop_error;
static SemaphoreHandle_t s_hs_synced_sem = NULL;
static uint16_t s_conn_handles[MAX_BLE_CONNECTIONS];
static atomic_int s_conn_count = 0;
static _Atomic(uint8_t) s_max_conn = DEFAULT_MAX_CONN;
static _Atomic(bluetooth_service_sniffer_cb_t) s_sniffer_cb;
static _Atomic(bluetooth_service_tracker_cb_t) s_tracker_cb;
static uint8_t s_tracker_target_addr[BLE_MAC_LEN];
static bluetooth_service_scan_result_t s_scan_results[BLE_SCAN_LIST_SIZE];
static uint16_t s_scan_count = 0;
static SemaphoreHandle_t s_scan_sem = NULL;

static void on_reset(int reason);
static void on_sync(void);
static void host_task(void *param);
static int gap_event(struct ble_gap_event *event, void *arg);
static void load_announce_config(char *out_name, uint8_t *out_max_conn);

static esp_err_t init_locked(void);
static esp_err_t start_locked(void);
static esp_err_t stop_locked(void);
static esp_err_t deinit_locked(void);
static void stop_task(void *param);

esp_err_t bluetooth_service_init(void) {
  if (atomic_flag_test_and_set(&s_lifecycle_busy))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = init_locked();
  atomic_flag_clear(&s_lifecycle_busy);
  return error;
}

esp_err_t bluetooth_service_start(void) {
  if (atomic_flag_test_and_set(&s_lifecycle_busy))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = start_locked();
  atomic_flag_clear(&s_lifecycle_busy);
  return error;
}

esp_err_t bluetooth_service_stop(void) {
  if (atomic_flag_test_and_set(&s_lifecycle_busy))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = stop_locked();
  atomic_flag_clear(&s_lifecycle_busy);
  return error;
}

esp_err_t bluetooth_service_deinit(void) {
  if (atomic_flag_test_and_set(&s_lifecycle_busy))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = deinit_locked();
  atomic_flag_clear(&s_lifecycle_busy);
  return error;
}

bool bluetooth_service_is_initialized(void) {
  return s_has_host || s_has_controller;
}

bool bluetooth_service_is_running(void) {
  return s_is_running && s_is_synced && !s_has_fault && !s_is_stopping;
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

  if (cb == NULL)
    cb = gap_event;

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
  if (!bluetooth_service_is_running())
    return ESP_ERR_INVALID_STATE;

  s_adv_requested = true;
  if (s_conn_count >= s_max_conn)
    return ESP_OK;
  if (ble_gap_adv_active()) {
    ESP_LOGI(TAG, "Advertising already active");
    return ESP_OK;
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

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error setting advertisement fields: rc=%d", rc);
    return ESP_FAIL;
  }

  const char *device_name = ble_svc_gap_device_name();
  struct ble_hs_adv_fields scan_response = {0};
  scan_response.name = (uint8_t *)device_name;
  size_t name_length = strlen(device_name);
  scan_response.name_len =
      name_length > BLE_ADVERTISED_NAME_MAX ? BLE_ADVERTISED_NAME_MAX : name_length;
  scan_response.name_is_complete = name_length <= BLE_ADVERTISED_NAME_MAX;
  if (ble_gap_adv_rsp_set_fields(&scan_response) != 0) {
    s_adv_requested = false;
    return ESP_FAIL;
  }

  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
  if (rc != 0) {
    s_adv_requested = false;
    ESP_LOGE(TAG, "Error starting advertisement: rc=%d", rc);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Advertising started");
  return ESP_OK;
}

esp_err_t bluetooth_service_stop_advertising(void) {
  s_adv_requested = false;
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
  if (name == NULL || strnlen(name, BLE_NAME_MAX_LEN + 1) == 0 ||
      strnlen(name, BLE_NAME_MAX_LEN + 1) > BLE_ADVERTISED_NAME_MAX || max_conn < 1 ||
      max_conn > CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL || cJSON_AddStringToObject(root, "ssid", name) == NULL ||
      cJSON_AddNumberToObject(root, "max_conn", max_conn) == NULL) {
    ESP_LOGE(TAG, "Cannot allocate BLE settings");
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
    ESP_LOGE(TAG, "Cannot serialize BLE settings");
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = storage_assets_write_file(BLE_ANNOUNCE_CONFIG_FILE, json_string);
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
  if (atomic_flag_test_and_set(&s_lifecycle_busy))
    return;
  bool expected = false;
  if (!bluetooth_service_is_running() || duration_ms == 0 || duration_ms > BLE_SCAN_MAX_MS ||
      !atomic_compare_exchange_strong(&s_is_scanning, &expected, true)) {
    ESP_LOGW(TAG, "BLE scan unavailable");
    atomic_flag_clear(&s_lifecycle_busy);
    return;
  }
  bool was_advertising = s_adv_requested;
  bluetooth_service_stop_advertising();
  xSemaphoreTake(s_scan_sem, 0);
  s_scan_count = 0;
  memset(s_scan_results, 0, sizeof(s_scan_results));
  struct ble_gap_disc_params disc_params = {0};
  int rc = ble_gap_disc(s_own_addr_type, duration_ms, &disc_params, gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error initiating GAP discovery: %d", rc);
    s_has_fault = true;
  } else if (xSemaphoreTake(s_scan_sem, pdMS_TO_TICKS(duration_ms + BLE_SCAN_MARGIN_MS)) !=
             pdTRUE) {
    ESP_LOGE(TAG, "BLE scan timed out");
    ble_gap_disc_cancel();
    s_has_fault = true;
  }
  if (s_has_fault)
    s_scan_count = 0;
  else if (was_advertising)
    bluetooth_service_start_advertising();
  s_is_scanning = false;
  atomic_flag_clear(&s_lifecycle_busy);
}

esp_err_t bluetooth_service_start_sniffer(bluetooth_service_sniffer_cb_t cb) {
  if (!bluetooth_service_is_running()) {
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
    s_has_fault = true;
    s_sniffer_cb = NULL;
    return ESP_FAIL;
  }
  return ESP_OK;
}

void bluetooth_service_stop_sniffer(void) {
  if (s_is_initialized && ble_gap_disc_active()) {
    ble_gap_disc_cancel();
  }
  s_sniffer_cb = NULL;
  ESP_LOGI(TAG, "Sniffer stopped");
}

esp_err_t bluetooth_service_start_tracker(const uint8_t *addr, bluetooth_service_tracker_cb_t cb) {
  if (!bluetooth_service_is_running()) {
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
    s_has_fault = true;
    s_tracker_cb = NULL;
    return ESP_FAIL;
  }
  return ESP_OK;
}

void bluetooth_service_stop_tracker(void) {
  if (s_is_initialized && ble_gap_disc_active()) {
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

static esp_err_t init_locked(void) {
  if (s_is_initialized && !s_has_fault && !s_is_stopping)
    return ESP_OK;
  esp_err_t error = deinit_locked();
  if (error != ESP_OK)
    return error;
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE)
    return ESP_ERR_INVALID_STATE;
  error = nvs_flash_init();
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "NVS unavailable: %s", esp_err_to_name(error));
    return error;
  }
  s_hs_synced_sem = xSemaphoreCreateBinary();
  s_host_exited_sem = xSemaphoreCreateBinary();
  s_stop_done_sem = xSemaphoreCreateBinary();
  s_scan_sem = xSemaphoreCreateBinary();
  if (!s_hs_synced_sem || !s_host_exited_sem || !s_stop_done_sem || !s_scan_sem) {
    ESP_LOGE(TAG, "Cannot allocate BLE lifecycle semaphores");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  error = nimble_port_init();
  s_has_controller = esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE;
  if (error != ESP_OK)
    goto cleanup;
  s_has_host = true;
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  char device_name[BLE_NAME_MAX_LEN + 1] = "Darth Maul";
  uint8_t max_conn = DEFAULT_MAX_CONN;
  load_announce_config(device_name, &max_conn);
  s_max_conn =
      max_conn >= 1 && max_conn <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS ? max_conn : DEFAULT_MAX_CONN;
  if (ble_svc_gap_device_name_set(device_name) != 0) {
    error = ESP_FAIL;
    goto cleanup;
  }
  s_is_initialized = true;
  s_has_fault = false;
  ESP_LOGI(TAG, "BLE initialized");
  return ESP_OK;
cleanup:
  ESP_LOGE(TAG, "BLE initialization failed: %s", esp_err_to_name(error));
  s_has_fault = true;
  esp_err_t cleanup_error = deinit_locked();
  if (cleanup_error != ESP_OK)
    ESP_LOGE(TAG, "BLE cleanup pending: %s", esp_err_to_name(cleanup_error));
  return error;
}

static esp_err_t start_locked(void) {
  esp_err_t error = init_locked();
  if (error != ESP_OK)
    return error;
  if (bluetooth_service_is_running())
    return ESP_OK;
  if (s_host_task != NULL)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_hs_synced_sem, 0);
  xSemaphoreTake(s_host_exited_sem, 0);
  s_sync_error = ESP_ERR_TIMEOUT;
  s_is_running = true;
  if (xTaskCreatePinnedToCore(host_task,
                              "ble_host",
                              NIMBLE_HS_STACK_SIZE,
                              NULL,
                              SYS_PRIO_BLE_HOST,
                              &s_host_task,
                              SYS_CORE_MAIN) != pdPASS) {
    s_is_running = false;
    s_host_task = NULL;
    error = ESP_ERR_NO_MEM;
  } else if (xSemaphoreTake(s_hs_synced_sem, pdMS_TO_TICKS(BLE_SYNC_TIMEOUT_MS)) != pdTRUE) {
    error = ESP_ERR_TIMEOUT;
  } else {
    error = s_sync_error;
    if (!bluetooth_service_is_running())
      error = ESP_FAIL;
  }
  if (error == ESP_OK) {
    ESP_LOGI(TAG, "BLE ready");
    return ESP_OK;
  }
  ESP_LOGE(TAG, "BLE start failed: %s", esp_err_to_name(error));
  s_has_fault = true;
  esp_err_t cleanup_error = deinit_locked();
  if (cleanup_error != ESP_OK)
    ESP_LOGE(TAG, "BLE cleanup pending: %s", esp_err_to_name(cleanup_error));
  return error;
}

static esp_err_t stop_locked(void) {
  if (gatt_explorer_is_busy() || s_is_scanning)
    return ESP_ERR_INVALID_STATE;
  s_adv_requested = false;
  s_is_stopping = true;
  s_is_synced = false;
  if (s_host_task == NULL)
    return ESP_OK;
  if (s_stop_task == NULL && s_is_running && !s_has_stop_ack) {
    xSemaphoreTake(s_stop_done_sem, 0);
    s_stop_error = ESP_FAIL;
    if (xTaskCreatePinnedToCore(stop_task,
                                "ble_stop",
                                BLE_STOP_STACK_SIZE,
                                NULL,
                                SYS_PRIO_SERVICE_HI,
                                &s_stop_task,
                                SYS_CORE_MAIN) != pdPASS) {
      s_stop_task = NULL;
      s_has_fault = true;
      ESP_LOGE(TAG, "Cannot allocate BLE stop task");
      return ESP_ERR_NO_MEM;
    }
  }
  if (s_stop_task != NULL) {
    if (xSemaphoreTake(s_stop_done_sem, pdMS_TO_TICKS(BLE_STOP_TIMEOUT_MS)) != pdTRUE) {
      s_has_fault = true;
      ESP_LOGE(TAG, "BLE stop pending; retaining host resources");
      return ESP_ERR_TIMEOUT;
    }
    vTaskDelete(s_stop_task);
    s_stop_task = NULL;
    if (s_stop_error != 0) {
      s_has_fault = true;
      ESP_LOGE(TAG, "NimBLE stop failed: %d", (int)s_stop_error);
      return ESP_FAIL;
    }
    s_has_stop_ack = true;
  }
  if (xSemaphoreTake(s_host_exited_sem, pdMS_TO_TICKS(BLE_HOST_EXIT_TIMEOUT_MS)) != pdTRUE) {
    s_has_fault = true;
    return ESP_ERR_TIMEOUT;
  }
  vTaskDelete(s_host_task);
  s_host_task = NULL;
  s_has_stop_ack = false;
  s_is_running = false;
  // A stopped NimBLE event loop must be rebuilt before its next start.
  s_has_fault = true;
  return ESP_OK;
}

static esp_err_t deinit_locked(void) {
  esp_err_t error = stop_locked();
  if (error != ESP_OK)
    return error;
  if (s_has_host) {
    error = gatt_explorer_deinit();
    if (error != ESP_OK)
      return error;
    error = esp_nimble_deinit();
    if (error != ESP_OK)
      return error;
    s_has_host = false;
    s_is_initialized = false;
    ble_hid_stack_reset();
  }
  // Retry only the unfinished controller phase after a partial teardown.
  if (s_has_controller) {
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
      error = esp_bt_controller_disable();
      if (error != ESP_OK)
        return error;
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
      error = esp_bt_controller_deinit();
      if (error != ESP_OK)
        return error;
    }
    s_has_controller = false;
  }
  SemaphoreHandle_t *semaphores[] = {
      &s_hs_synced_sem, &s_host_exited_sem, &s_stop_done_sem, &s_scan_sem};
  for (size_t i = 0; i < sizeof(semaphores) / sizeof(semaphores[0]); ++i) {
    if (*semaphores[i] != NULL) {
      vSemaphoreDelete(*semaphores[i]);
      *semaphores[i] = NULL;
    }
  }
  s_is_initialized = s_is_running = s_is_synced = s_is_stopping = s_has_fault = false;
  s_sniffer_cb = NULL;
  s_tracker_cb = NULL;
  s_conn_count = 0;
  return ESP_OK;
}

static void on_reset(int reason) {
  s_is_synced = false;
  s_has_fault = true;
  s_adv_requested = false;
  s_conn_count = 0;
  s_sync_error = ESP_FAIL;
  if (s_hs_synced_sem != NULL)
    xSemaphoreGive(s_hs_synced_sem);
  if (s_is_scanning && s_scan_sem != NULL)
    xSemaphoreGive(s_scan_sem);
  ESP_LOGE(TAG, "BLE host reset: reason=%d; recovery required", reason);
}

static void on_sync(void) {
  if (s_is_stopping)
    return;
  int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
  s_sync_error = rc == 0 ? ESP_OK : ESP_FAIL;
  s_is_synced = rc == 0;
  if (rc != 0) {
    s_has_fault = true;
    ESP_LOGE(TAG, "Error inferring address type: %d", rc);
  }
  if (s_hs_synced_sem != NULL)
    xSemaphoreGive(s_hs_synced_sem);
}

static void host_task(void *param) {
  (void)param;
  nimble_port_run();
  s_is_running = false;
  s_is_synced = false;
  xSemaphoreGive(s_host_exited_sem);
  vTaskSuspend(NULL);
}

static void stop_task(void *param) {
  (void)param;
  s_stop_error = nimble_port_stop();
  xSemaphoreGive(s_stop_done_sem);
  vTaskSuspend(NULL);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      ESP_LOGI(TAG,
               "Device connected, status=%d conn_handle=%d",
               event->connect.status,
               event->connect.conn_handle);
      if (event->connect.status == 0) {
        if (s_conn_count >= s_max_conn) {
          ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
          break;
        }
        s_conn_handles[s_conn_count++] = event->connect.conn_handle;
        if (s_adv_requested && s_conn_count < s_max_conn)
          bluetooth_service_start_advertising();
      } else if (s_adv_requested) {
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
      if (s_adv_requested)
        bluetooth_service_start_advertising();
      break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      ESP_LOGI(TAG, "Advertising complete");
      if (s_adv_requested)
        bluetooth_service_start_advertising();
      break;

    case BLE_GAP_EVENT_DISC: {
      bluetooth_service_sniffer_cb_t sniffer_cb = s_sniffer_cb;
      bluetooth_service_tracker_cb_t tracker_cb = s_tracker_cb;
      if (sniffer_cb != NULL) {
        sniffer_cb(event->disc.addr.val,
                   event->disc.addr.type,
                   event->disc.rssi,
                   event->disc.data,
                   event->disc.length_data);
        return 0;
      }

      if (tracker_cb != NULL) {
        if (memcmp(event->disc.addr.val, s_tracker_target_addr, BLE_MAC_LEN) == 0) {
          tracker_cb(event->disc.rssi);
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
      if (cJSON_IsNumber(j_conn) && j_conn->valueint >= 1 &&
          j_conn->valueint <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
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
