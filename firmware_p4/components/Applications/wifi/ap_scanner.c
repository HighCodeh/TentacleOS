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
#include "cJSON.h"
#include "storage_write.h"
#include "sd_card_write.h"
#include "sd_card_init.h"

static const char *TAG = "AP_SCANNER";

static TaskHandle_t scanner_task_handle = NULL;
static StackType_t *scanner_task_stack = NULL;
static StaticTask_t *scanner_task_tcb = NULL;
#define SCANNER_STACK_SIZE 4096

static wifi_ap_record_t *scan_results = NULL;
static uint16_t scan_count = 0;
static bool is_scanning = false;

static const char* get_auth_mode_name(wifi_auth_mode_t auth_mode) {
  switch (auth_mode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK: return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3-PSK";
    default: return "Unknown";
  }
}

static bool save_results_to_path(const char *path, bool use_sd_driver) {
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
    wifi_ap_record_t *ap = &scan_results[i];
    cJSON *entry = cJSON_CreateObject();

    cJSON_AddStringToObject(entry, "ssid", (char *)ap->ssid);

    char bssid_str[18];
    snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             ap->bssid[0], ap->bssid[1], ap->bssid[2],
             ap->bssid[3], ap->bssid[4], ap->bssid[5]);
    cJSON_AddStringToObject(entry, "bssid", bssid_str);

    cJSON_AddNumberToObject(entry, "rssi", ap->rssi);
    cJSON_AddNumberToObject(entry, "channel", ap->primary);
    cJSON_AddNumberToObject(entry, "authmode", ap->authmode);
    cJSON_AddStringToObject(entry, "auth_str", get_auth_mode_name(ap->authmode));
    cJSON_AddBoolToObject(entry, "wps", ap->wps);

    cJSON_AddItemToArray(root, entry);
  }

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
    ESP_LOGE(TAG, "Failed to print JSON.");
    cJSON_Delete(root);
    return false;
  }

  esp_err_t err;
  if (use_sd_driver) {
    if (!sd_is_mounted()) {
      ESP_LOGE(TAG, "SD Card not mounted.");
      free(json_string);
      cJSON_Delete(root);
      return false;
    }
    err = sd_write_string(path, json_string);
  } else {
    err = storage_write_string(path, json_string);
  }

  free(json_string);
  cJSON_Delete(root);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write results to %s: %s", path, esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "Scan results saved to %s", path);
  return true;
}

bool ap_scanner_save_results_to_internal_flash(void) {
  return save_results_to_path("/assets/storage/wifi/scanned_aps.json", false);
}

bool ap_scanner_save_results_to_sd_card(void) {
  return save_results_to_path("/scanned_aps.json", true); 
}

static void ap_scanner_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting Wi-Fi Scan Task (PSRAM)...");

  wifi_service_scan();

  uint16_t ap_num = wifi_service_get_ap_count();
  ESP_LOGI(TAG, "Scan completed. Service found %d APs.", ap_num);

  if (scan_results) {
    heap_caps_free(scan_results);
    scan_results = NULL;
    scan_count = 0;
  }

  if (ap_num > 0) {
    scan_results = (wifi_ap_record_t *)heap_caps_malloc(ap_num * sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM);
    if (scan_results) {
      scan_count = ap_num;
      for (int i = 0; i < ap_num; i++) {
        wifi_ap_record_t *rec = wifi_service_get_ap_record(i);
        if (rec) {
          memcpy(&scan_results[i], rec, sizeof(wifi_ap_record_t));
          ESP_LOGI(TAG, "[%d] SSID: %s | CH: %d | RSSI: %d | Auth: %d", 
                   i, rec->ssid, rec->primary, rec->rssi, rec->authmode);
        }
      }
      ESP_LOGI(TAG, "Results copied to PSRAM.");

      ap_scanner_save_results_to_internal_flash();

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
    scanner_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
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
