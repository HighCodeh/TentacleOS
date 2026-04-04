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

#include "ble_scanner.h"
#include "bluetooth_service.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "cJSON.h"
#include "storage_write.h"
#include "tos_storage_paths.h"
#include "tos_loot.h"
#include "oui_lookup.h"

static const char *TAG = "BLE_SCANNER";

static TaskHandle_t scanner_task_handle = NULL;
static StackType_t *scanner_task_stack = NULL;
static StaticTask_t *scanner_task_tcb = NULL;
#define SCANNER_STACK_SIZE 4096

static ble_scan_result_t *scan_results = NULL;
static uint16_t scan_count = 0;
static bool is_scanning = false;

static bool save_results_to_loot(void) {
  if (scan_results == NULL || scan_count == 0) {
    ESP_LOGW(TAG, "No results to save.");
    return false;
  }

  cJSON *root = cJSON_CreateArray();
  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to create JSON array.");
    return false;
  }

  for (int i = 0; i < scan_count; i++) {
    ble_scan_result_t *dev = &scan_results[i];
    cJSON *entry = cJSON_CreateObject();

    cJSON_AddStringToObject(entry, "name", strlen(dev->name) > 0 ? dev->name : "Unknown");

    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             dev->addr[5], dev->addr[4], dev->addr[3],
             dev->addr[2], dev->addr[1], dev->addr[0]);

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
  if (!json_string) {
    ESP_LOGE(TAG, "Failed to print JSON.");
    return false;
  }

  char path[128];
  tos_loot_generate_path(TOS_PATH_BLE_LOOT, "ble_scan", "json",
                         path, sizeof(path), NULL, 0);

  esp_err_t err = storage_write_string(path, json_string);
  free(json_string);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save: %s", path);
    return false;
  }

  ESP_LOGI(TAG, "Scan results saved to %s", path);
  return true;
}

static void ble_scanner_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting BLE Scan Task (PSRAM)...");

  // Scan for 10 seconds
  bluetooth_service_scan(10000);

  uint16_t dev_num = bluetooth_service_get_scan_count();
  ESP_LOGI(TAG, "Scan completed. Service found %d devices.", dev_num);

  if (scan_results) {
    heap_caps_free(scan_results);
    scan_results = NULL;
    scan_count = 0;
  }

  if (dev_num > 0) {
    scan_results = (ble_scan_result_t *)heap_caps_malloc(dev_num * sizeof(ble_scan_result_t), MALLOC_CAP_SPIRAM);
    if (scan_results) {
      scan_count = dev_num;
      for (int i = 0; i < dev_num; i++) {
        ble_scan_result_t *rec = bluetooth_service_get_scan_result(i);
        if (rec) {
          memcpy(&scan_results[i], rec, sizeof(ble_scan_result_t));
          ESP_LOGI(TAG, "[%d] Name: %s | RSSI: %d", i, rec->name, rec->rssi);
        }
      }
      ESP_LOGI(TAG, "Results copied to PSRAM.");

      save_results_to_loot();

    } else {
      ESP_LOGE(TAG, "Failed to allocate memory for results in PSRAM!");
    }
  }

  is_scanning = false;
  scanner_task_handle = NULL;
  vTaskDelete(NULL);
}

bool ble_scanner_start(void) {
  if (is_scanning) {
    ESP_LOGW(TAG, "Scan already in progress.");
    return false;
  }

  if (scanner_task_stack == NULL) {
    scanner_task_stack = (StackType_t *)heap_caps_malloc(SCANNER_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  }
  if (scanner_task_tcb == NULL) {
    scanner_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
  }

  if (scanner_task_stack == NULL || scanner_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate scanner task memory in PSRAM!");
    if (scanner_task_stack) { heap_caps_free(scanner_task_stack); scanner_task_stack = NULL; }
    if (scanner_task_tcb) { heap_caps_free(scanner_task_tcb); scanner_task_tcb = NULL; }
    return false;
  }

  is_scanning = true;
  scanner_task_handle = xTaskCreateStatic(
    ble_scanner_task,
    "ble_scan_task",
    SCANNER_STACK_SIZE,
    NULL,
    5,
    scanner_task_stack,
    scanner_task_tcb
  );

  return (scanner_task_handle != NULL);
}

ble_scan_result_t* ble_scanner_get_results(uint16_t *count) {
  if (is_scanning) {
    return NULL;
  }
  *count = scan_count;
  return scan_results;
}

void ble_scanner_free_results(void) {
  if (scan_results) {
    heap_caps_free(scan_results);
    scan_results = NULL;
  }
  scan_count = 0;

  if (!is_scanning) {
    if (scanner_task_stack) { heap_caps_free(scanner_task_stack); scanner_task_stack = NULL; }
    if (scanner_task_tcb) { heap_caps_free(scanner_task_tcb); scanner_task_tcb = NULL; }
  }
}
