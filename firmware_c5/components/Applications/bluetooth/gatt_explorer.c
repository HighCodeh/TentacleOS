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

#include "gatt_explorer.h"

#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"

#include "bluetooth_service.h"
#include "storage_write.h"
#include "tos_flash_paths.h"
#include "uuid_lookup.h"

static const char *TAG = "GATT_EXPLORER";

#define MAX_DISCOVERED_SVCS    16
#define EXPLORER_STACK_SIZE    6144
#define EXPLORER_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define EXPLORER_TIMEOUT_MS    60000
#define ADDR_STR_LEN           18
#define SVC_NAME_MAX_LEN       64
#define UUID_STR_MAX_LEN       48

typedef struct {
  uint16_t start_handle;
  uint16_t end_handle;
  ble_uuid_any_t uuid;
  char name[SVC_NAME_MAX_LEN];
} gatt_explorer_svc_t;

static gatt_explorer_svc_t s_svcs[MAX_DISCOVERED_SVCS];
static int s_svc_count = 0;
static int s_current_svc_idx = 0;

static cJSON *s_root_json = NULL;
static cJSON *s_svcs_json = NULL;
static uint16_t s_conn_handle = 0;
static bool s_is_busy = false;

static TaskHandle_t s_explorer_task_handle = NULL;
static StackType_t *s_explorer_task_stack = NULL;
static StaticTask_t *s_explorer_task_tcb = NULL;

static int on_disc_chr(uint16_t conn_handle,
                       const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr,
                       void *arg);
static void explorer_task(void *pvParameters);

static void signal_save_and_exit(void) {
  if (s_explorer_task_handle != NULL) {
    xTaskNotify(s_explorer_task_handle, 1, eSetBits);
  }
}

static int on_disc_svc(uint16_t conn_h,
                       const struct ble_gatt_error *error,
                       const struct ble_gatt_svc *svc,
                       void *arg) {
  if (error->status == BLE_HS_EDONE) {
    if (s_svc_count > 0) {
      s_current_svc_idx = 0;
      ble_gattc_disc_all_chrs(s_conn_handle,
                              s_svcs[s_current_svc_idx].start_handle,
                              s_svcs[s_current_svc_idx].end_handle,
                              on_disc_chr,
                              NULL);
    } else {
      signal_save_and_exit();
    }
    return 0;
  }

  if (error->status != 0) {
    ESP_LOGE(TAG, "Service discovery error: %d", error->status);
    signal_save_and_exit();
    return 0;
  }

  if (s_svc_count < MAX_DISCOVERED_SVCS) {
    s_svcs[s_svc_count].start_handle = svc->start_handle;
    s_svcs[s_svc_count].end_handle = svc->end_handle;
    memcpy(&s_svcs[s_svc_count].uuid, &svc->uuid, sizeof(ble_uuid_any_t));
    strncpy(s_svcs[s_svc_count].name, uuid_get_name(&svc->uuid.u), SVC_NAME_MAX_LEN - 1);

    cJSON *s = cJSON_CreateObject();
    char uuid_str[UUID_STR_MAX_LEN];
    ble_uuid_to_str(&svc->uuid.u, uuid_str);
    cJSON_AddStringToObject(s, "uuid", uuid_str);
    cJSON_AddStringToObject(s, "name", s_svcs[s_svc_count].name);
    cJSON_AddItemToArray(s_svcs_json, s);
    cJSON_AddItemToObject(s, "characteristics", cJSON_CreateArray());

    s_svc_count++;
  }
  return 0;
}

static int on_disc_chr(uint16_t conn_h,
                       const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr,
                       void *arg) {
  if (error->status == BLE_HS_EDONE) {
    s_current_svc_idx++;
    if (s_current_svc_idx < s_svc_count) {
      ble_gattc_disc_all_chrs(s_conn_handle,
                              s_svcs[s_current_svc_idx].start_handle,
                              s_svcs[s_current_svc_idx].end_handle,
                              on_disc_chr,
                              NULL);
    } else {
      signal_save_and_exit();
    }
    return 0;
  }

  cJSON *s = cJSON_GetArrayItem(s_svcs_json, s_current_svc_idx);
  cJSON *chrs = cJSON_GetObjectItem(s, "characteristics");
  cJSON *c = cJSON_CreateObject();
  char uuid_str[UUID_STR_MAX_LEN];
  ble_uuid_to_str(&chr->uuid.u, uuid_str);
  cJSON_AddStringToObject(c, "uuid", uuid_str);
  cJSON_AddStringToObject(c, "name", uuid_get_name(&chr->uuid.u));
  cJSON_AddNumberToObject(c, "handle", chr->val_handle);
  cJSON_AddNumberToObject(c, "properties", chr->properties);
  cJSON_AddItemToArray(chrs, c);

  return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg) {
  if (event->type == BLE_GAP_EVENT_CONNECT) {
    if (event->connect.status == 0) {
      s_conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, "Connected. Discovering services...");
      ble_gattc_disc_all_svcs(s_conn_handle, on_disc_svc, NULL);
    } else {
      ESP_LOGE(TAG, "Connection failed: %d", event->connect.status);
      signal_save_and_exit();
    }
  } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
    ESP_LOGI(TAG, "Disconnected.");
    // If disconnected unexpectedly, save what we have
    signal_save_and_exit();
  }
  return 0;
}

bool gatt_explorer_start(const uint8_t *addr, uint8_t addr_type) {
  if (s_is_busy)
    return false;
  s_is_busy = true;
  s_svc_count = 0;

  s_root_json = cJSON_CreateObject();
  char addr_str[ADDR_STR_LEN];
  snprintf(addr_str,
           ADDR_STR_LEN,
           "%02x:%02x:%02x:%02x:%02x:%02x",
           addr[5],
           addr[4],
           addr[3],
           addr[2],
           addr[1],
           addr[0]);
  cJSON_AddStringToObject(s_root_json, "target", addr_str);
  s_svcs_json = cJSON_AddArrayToObject(s_root_json, "services");

  s_explorer_task_stack =
      (StackType_t *)heap_caps_malloc(EXPLORER_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_explorer_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);

  if (s_explorer_task_stack == NULL || s_explorer_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM for GATT Task");
    free(s_explorer_task_stack);
    free(s_explorer_task_tcb);
    s_explorer_task_stack = NULL;
    s_explorer_task_tcb = NULL;
    cJSON_Delete(s_root_json);
    s_is_busy = false;
    return false;
  }

  s_explorer_task_handle = xTaskCreateStatic(explorer_task,
                                             "gatt_task",
                                             EXPLORER_STACK_SIZE,
                                             NULL,
                                             EXPLORER_TASK_PRIORITY,
                                             s_explorer_task_stack,
                                             s_explorer_task_tcb);

  if (bluetooth_service_connect(addr, addr_type, gap_event) != ESP_OK) {
    xTaskNotify(s_explorer_task_handle, 0, eNoAction);
    signal_save_and_exit();
    return false;
  }
  return true;
}

bool gatt_explorer_is_busy(void) {
  return s_is_busy;
}

static void explorer_task(void *pvParameters) {
  uint32_t notification_value = 0;

  if (xTaskNotifyWait(0, 0, &notification_value, pdMS_TO_TICKS(EXPLORER_TIMEOUT_MS)) == pdTRUE) {
    if (notification_value & 1) {
      if (s_root_json != NULL) {
        ESP_LOGI(TAG, "Saving GATT results to storage...");
        char *json_str = cJSON_Print(s_root_json);
        if (json_str != NULL) {
          storage_write_string(FLASH_STORAGE_BLE_GATT, json_str);
          free(json_str);
        }
        cJSON_Delete(s_root_json);
        s_root_json = NULL;
      }
    }
  } else {
    ESP_LOGE(TAG, "GATT Exploration Timed Out.");
    if (s_root_json != NULL) {
      cJSON_Delete(s_root_json);
      s_root_json = NULL;
    }
  }

  bluetooth_service_disconnect_all();

  s_is_busy = false;

  if (s_explorer_task_stack != NULL) {
    free(s_explorer_task_stack);
    s_explorer_task_stack = NULL;
  }
  if (s_explorer_task_tcb != NULL) {
    free(s_explorer_task_tcb);
    s_explorer_task_tcb = NULL;
  }
  s_explorer_task_handle = NULL;
  vTaskDelete(NULL);
}
