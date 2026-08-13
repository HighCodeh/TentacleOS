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

#include "ap_scanner.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "cJSON.h"
#include "sd_card_init.h"
#include "sd_card_write.h"
#include "storage_write.h"
#include "tos_flash_paths.h"
#include "wifi_service.h"

static const char *TAG = "AP_SCANNER";

#define SCANNER_STACK_SIZE    4096
#define SCANNER_TASK_PRIORITY SYS_PRIO_SERVICE_HI

static TaskHandle_t s_scanner_task_handle = NULL;
static StackType_t *s_scanner_task_stack = NULL;
static StaticTask_t *s_scanner_task_tcb = NULL;

static wifi_ap_record_t *s_scan_results = NULL;
static uint16_t s_scan_count = 0;
static bool s_is_scanning = false;

static const char *get_auth_mode_name(wifi_auth_mode_t auth_mode);
static bool save_results_to_path(const char *path, bool use_sd_driver);
static void scanner_task(void *pvParameters);

bool ap_scanner_start(void) {
  if (s_is_scanning) {
    ESP_LOGW(TAG, "Scan already in progress.");
    return false;
  }

  if (s_scanner_task_stack == NULL) {
    s_scanner_task_stack = (StackType_t *)heap_caps_malloc(SCANNER_STACK_SIZE * sizeof(StackType_t),
                                                           MALLOC_CAP_SPIRAM);
  }
  if (s_scanner_task_tcb == NULL) {
    s_scanner_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t),
                                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (s_scanner_task_stack == NULL || s_scanner_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate scanner task memory in PSRAM!");
    if (s_scanner_task_stack != NULL) {
      free(s_scanner_task_stack);
      s_scanner_task_stack = NULL;
    }
    if (s_scanner_task_tcb != NULL) {
      free(s_scanner_task_tcb);
      s_scanner_task_tcb = NULL;
    }
    return false;
  }

  s_is_scanning = true;
  s_scanner_task_handle = xTaskCreateStatic(scanner_task,
                                            "ap_scan_task",
                                            SCANNER_STACK_SIZE,
                                            NULL,
                                            SCANNER_TASK_PRIORITY,
                                            s_scanner_task_stack,
                                            s_scanner_task_tcb);

  return (s_scanner_task_handle != NULL);
}

wifi_ap_record_t *ap_scanner_get_results(uint16_t *out_count) {
  if (s_is_scanning) {
    return NULL;
  }
  *out_count = s_scan_count;
  return s_scan_results;
}

void ap_scanner_free_results(void) {
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

bool ap_scanner_save_results_to_internal_flash(void) {
  return save_results_to_path(FLASH_STORAGE_WIFI_APS, false);
}

bool ap_scanner_save_results_to_sd_card(void) {
  return save_results_to_path("/scanned_aps.json", true);
}

static const char *get_auth_mode_name(wifi_auth_mode_t auth_mode) {
  switch (auth_mode) {
    case WIFI_AUTH_OPEN:
      return "OPEN";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3-PSK";
    default:
      return "Unknown";
  }
}

static bool save_results_to_path(const char *path, bool use_sd_driver) {
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
    wifi_ap_record_t *ap = &s_scan_results[i];
    cJSON *entry = cJSON_CreateObject();

    cJSON_AddStringToObject(entry, "ssid", (char *)ap->ssid);

    char bssid_str[18];
    snprintf(bssid_str,
             sizeof(bssid_str),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             ap->bssid[0],
             ap->bssid[1],
             ap->bssid[2],
             ap->bssid[3],
             ap->bssid[4],
             ap->bssid[5]);
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

static void scanner_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting Wi-Fi Scan Task (PSRAM)...");

  wifi_service_scan();

  uint16_t ap_num = wifi_service_get_ap_count();
  ESP_LOGI(TAG, "Scan completed. Service found %d APs.", ap_num);

  if (s_scan_results != NULL) {
    free(s_scan_results);
    s_scan_results = NULL;
    s_scan_count = 0;
  }

  if (ap_num > 0) {
    s_scan_results =
        (wifi_ap_record_t *)heap_caps_malloc(ap_num * sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM);
    if (s_scan_results != NULL) {
      s_scan_count = ap_num;
      for (int i = 0; i < ap_num; i++) {
        wifi_ap_record_t *rec = wifi_service_get_ap_record(i);
        if (rec != NULL) {
          memcpy(&s_scan_results[i], rec, sizeof(wifi_ap_record_t));
          ESP_LOGI(TAG,
                   "[%d] SSID: %s | CH: %d | RSSI: %d | Auth: %d",
                   i,
                   rec->ssid,
                   rec->primary,
                   rec->rssi,
                   rec->authmode);
        }
      }
      ESP_LOGI(TAG, "Results copied to PSRAM.");

      ap_scanner_save_results_to_internal_flash();

    } else {
      ESP_LOGE(TAG, "Failed to allocate memory for results in PSRAM!");
    }
  }

  s_is_scanning = false;
  s_scanner_task_handle = NULL;
  vTaskDelete(NULL);
}
