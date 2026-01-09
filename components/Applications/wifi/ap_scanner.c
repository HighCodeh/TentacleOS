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


#include "ap_scanner.h"
#include "wifi_service.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "AP_SCANNER";

// Task Handles
static TaskHandle_t scanner_task_handle = NULL;
static StackType_t *scanner_task_stack = NULL;
static StaticTask_t *scanner_task_tcb = NULL;
#define SCANNER_STACK_SIZE 4096

// Results in PSRAM
static wifi_ap_record_t *scan_results = NULL;
static uint16_t scan_count = 0;
static bool is_scanning = false;

static void ap_scanner_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting Wi-Fi Scan Task (PSRAM)...");

  // Use Service to perform the scan
  esp_err_t err = wifi_service_perform_full_scan();

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Scan failed via service");
    is_scanning = false;
    vTaskDelete(NULL);
    return;
  }

  uint16_t ap_num = 0;
  wifi_service_get_scan_count(&ap_num);
  ESP_LOGI(TAG, "Scan completed. Found %d APs.", ap_num);

  // TODO: add option to save results on internal flash or micro-sd

  if (scan_results) {
    heap_caps_free(scan_results);
    scan_results = NULL;
    scan_count = 0;
  }

  if (ap_num > 0) {
    scan_results = (wifi_ap_record_t *)heap_caps_malloc(ap_num * sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM);
    if (scan_results) {
      wifi_service_copy_scan_results(scan_results, ap_num);
      scan_count = ap_num;
      ESP_LOGI(TAG, "Results saved to PSRAM.");

      for (int i = 0; i < ap_num; i++) {
        ESP_LOGI(TAG, "[%d] SSID: %s | CH: %d | RSSI: %d | Auth: %d", 
                 i, scan_results[i].ssid, scan_results[i].primary, scan_results[i].rssi, scan_results[i].authmode);
      }
    } else {
      ESP_LOGE(TAG, "Failed to allocate memory for results in PSRAM!");
    }
  }

  is_scanning = false;
  scanner_task_handle = NULL;
  vTaskDelete(NULL);
}

bool ap_scanner_start(void) {
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
    ap_scanner_task,
    "ap_scan_task",
    SCANNER_STACK_SIZE,
    NULL,
    5,
    scanner_task_stack,
    scanner_task_tcb
  );

  return (scanner_task_handle != NULL);
}

wifi_ap_record_t* ap_scanner_get_results(uint16_t *count) {
  if (is_scanning) {
    return NULL;
  }
  *count = scan_count;
  return scan_results;
}

void ap_scanner_free_results(void) {
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
