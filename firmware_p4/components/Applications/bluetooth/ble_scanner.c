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

#include "ble_scanner.h"

#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "bluetooth_service.h"
#include "oui_lookup.h"
#include "storage_write.h"
#include "tos_loot.h"
#include "tos_storage_paths.h"

static const char *TAG = "BLE_SCANNER";

#define SCANNER_STACK_SIZE    4096
#define SCANNER_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define SCAN_DURATION_MS      10000

static TaskHandle_t s_scanner_task_handle = NULL;
static StackType_t *s_scanner_task_stack = NULL;
static StaticTask_t *s_scanner_task_tcb = NULL;

static bluetooth_service_scan_result_t *s_scan_results = NULL;
static uint16_t s_scan_count = 0;
static bool s_is_scanning = false;

static bool save_results_to_loot(void);
static void scanner_task(void *pvParameters);

bool ble_scanner_start(void) {
  if (s_is_scanning) {
    ESP_LOGW(TAG, "Scan already in progress.");
    return false;
  }

  if (s_scanner_task_stack == NULL) {
    s_scanner_task_stack = (StackType_t *)heap_caps_malloc(SCANNER_STACK_SIZE * sizeof(StackType_t),
                                                           MALLOC_CAP_SPIRAM);
  }
  if (s_scanner_task_tcb == NULL) {
    s_scanner_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
  }

  if (s_scanner_task_stack == NULL || s_scanner_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate scanner task memory in PSRAM!");
    free(s_scanner_task_stack);
    s_scanner_task_stack = NULL;
    free(s_scanner_task_tcb);
    s_scanner_task_tcb = NULL;
    return false;
  }

  s_is_scanning = true;
  s_scanner_task_handle = xTaskCreateStaticPinnedToCore(scanner_task,
                                                        "ble_scan_task",
                                                        SCANNER_STACK_SIZE,
                                                        NULL,
                                                        SCANNER_TASK_PRIORITY,
                                                        s_scanner_task_stack,
                                                        s_scanner_task_tcb,
                                                        SYS_CORE_RADIO);

  return (s_scanner_task_handle != NULL);
}

bluetooth_service_scan_result_t *ble_scanner_get_results(uint16_t *out_count) {
  if (s_is_scanning) {
    return NULL;
  }
  *out_count = s_scan_count;
  return s_scan_results;
}

void ble_scanner_free_results(void) {
  if (s_scan_results != NULL) {
    free(s_scan_results);
    s_scan_results = NULL;
  }
  s_scan_count = 0;

  if (!s_is_scanning) {
    if (s_scanner_task_stack != NULL) {
      free(s_scanner_task_stack);
      s_scanner_task_stack = NULL;
    }
    if (s_scanner_task_tcb != NULL) {
      free(s_scanner_task_tcb);
      s_scanner_task_tcb = NULL;
    }
  }
}

static bool save_results_to_loot(void) {
  if (s_scan_results == NULL || s_scan_count == 0) {
    ESP_LOGW(TAG, "No results to save.");
    return false;
  }

  cJSON *root = cJSON_CreateArray();
  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to create JSON array.");
    return false;
  }

  for (int i = 0; i < s_scan_count; i++) {
    bluetooth_service_scan_result_t *dev = &s_scan_results[i];
    cJSON *entry = cJSON_CreateObject();

    cJSON_AddStringToObject(entry, "name", strlen(dev->name) > 0 ? dev->name : "Unknown");

    char addr_str[18];
    snprintf(addr_str,
             sizeof(addr_str),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             dev->addr[5],
             dev->addr[4],
             dev->addr[3],
             dev->addr[2],
             dev->addr[1],
             dev->addr[0]);

    cJSON_AddStringToObject(entry, "addr", addr_str);
    cJSON_AddNumberToObject(entry, "addr_type", dev->addr_type);
    cJSON_AddNumberToObject(entry, "rssi", dev->rssi);

    const char *vendor = oui_get_vendor(dev->addr);
    cJSON_AddStringToObject(entry, "vendor", vendor);
    cJSON_AddStringToObject(entry, "uuids", strlen(dev->uuids) > 0 ? dev->uuids : "");

    cJSON_AddItemToArray(root, entry);
  }

  char *json_string = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json_string == NULL) {
    ESP_LOGE(TAG, "Failed to print JSON.");
    return false;
  }

  char path[128];
  tos_loot_generate_path(TOS_PATH_BLE_LOOT, "ble_scan", "json", path, sizeof(path), NULL, 0);

  esp_err_t err = storage_write_string(path, json_string);
  free(json_string);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save: %s", path);
    return false;
  }

  ESP_LOGI(TAG, "Scan results saved to %s", path);
  return true;
}

static void scanner_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting BLE Scan Task (PSRAM)...");

  bluetooth_service_scan(SCAN_DURATION_MS);

  uint16_t dev_num = bluetooth_service_get_scan_count();
  ESP_LOGI(TAG, "Scan completed. Service found %d devices.", dev_num);

  if (s_scan_results != NULL) {
    free(s_scan_results);
    s_scan_results = NULL;
    s_scan_count = 0;
  }

  if (dev_num > 0) {
    s_scan_results = (bluetooth_service_scan_result_t *)heap_caps_malloc(
        dev_num * sizeof(bluetooth_service_scan_result_t), MALLOC_CAP_SPIRAM);
    if (s_scan_results != NULL) {
      s_scan_count = dev_num;
      for (int i = 0; i < dev_num; i++) {
        bluetooth_service_scan_result_t *rec = bluetooth_service_get_scan_result(i);
        if (rec != NULL) {
          memcpy(&s_scan_results[i], rec, sizeof(bluetooth_service_scan_result_t));
          ESP_LOGI(TAG, "[%d] Name: %s | RSSI: %d", i, rec->name, rec->rssi);
        }
      }
      ESP_LOGI(TAG, "Results copied to PSRAM.");

      save_results_to_loot();

    } else {
      ESP_LOGE(TAG, "Failed to allocate memory for results in PSRAM!");
    }
  }

  s_is_scanning = false;
  s_scanner_task_handle = NULL;
  vTaskDelete(NULL);
}
