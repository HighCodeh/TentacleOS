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

#include "client_scanner.h"
#include "wifi_service.h"
#include "wifi_80211.h"
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
#include "mac_vendor.h"

static const char *TAG = "CLIENT_SCANNER";

static TaskHandle_t scanner_task_handle = NULL;
static StackType_t *scanner_task_stack = NULL;
static StaticTask_t *scanner_task_tcb = NULL;
#define SCANNER_STACK_SIZE 4096
#define SCAN_DURATION_MS 15000 

static client_record_t *scan_results = NULL;
static uint16_t scan_count = 0;
static uint16_t max_scan_results = 200; 
static bool is_scanning = false;

static void add_or_update_client(const uint8_t *bssid, const uint8_t *client_mac, int8_t rssi, uint8_t channel) {
  if (scan_results == NULL) return;

  for (int i = 0; i < scan_count; i++) {
    if (memcmp(scan_results[i].client_mac, client_mac, 6) == 0) {
      scan_results[i].rssi = rssi; 
      scan_results[i].channel = channel;
      memcpy(scan_results[i].bssid, bssid, 6);
      return;
    }
  }

  if (scan_count < max_scan_results) {
    memcpy(scan_results[scan_count].bssid, bssid, 6);
    memcpy(scan_results[scan_count].client_mac, client_mac, 6);
    scan_results[scan_count].rssi = rssi;
    scan_results[scan_count].channel = channel;
    scan_count++;
    ESP_LOGI(TAG, "New Client: %02x:%02x:%02x:%02x:%02x:%02x (BSSID: %02x:%02x:%02x:%02x:%02x:%02x) RSSI: %d",
             client_mac[0], client_mac[1], client_mac[2], client_mac[3], client_mac[4], client_mac[5],
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], rssi);
  }
}

static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_DATA) return;

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
  const wifi_mac_header_t *mac_header = (const wifi_mac_header_t *)ppkt->payload;
  const wifi_frame_control_t *fc = (const wifi_frame_control_t *)&mac_header->frame_control;

  if (fc->type != 2) return;

  uint8_t const *bssid = NULL;
  uint8_t const *client = NULL;

  if (fc->to_ds == 1 && fc->from_ds == 0) {
    bssid = mac_header->addr1;
    client = mac_header->addr2;
  } else if (fc->to_ds == 0 && fc->from_ds == 1) {
    client = mac_header->addr1;
    bssid = mac_header->addr2;
  } else {
    return;
  }

  if (client[0] & 0x01) return; 

  uint8_t channel = ppkt->rx_ctrl.channel;

  add_or_update_client(bssid, client, ppkt->rx_ctrl.rssi, channel);
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
    client_record_t *rec = &scan_results[i];

    cJSON *ap_entry = NULL;
    char bssid_str[18];
    snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             rec->bssid[0], rec->bssid[1], rec->bssid[2],
             rec->bssid[3], rec->bssid[4], rec->bssid[5]);

    int array_size = cJSON_GetArraySize(root);
    for (int j = 0; j < array_size; j++) {
      cJSON *item = cJSON_GetArrayItem(root, j);
      cJSON *bssid_obj = cJSON_GetObjectItem(item, "bssid");
      if (bssid_obj && strcmp(bssid_obj->valuestring, bssid_str) == 0) {
        ap_entry = item;
        break;
      }
    }

    if (ap_entry == NULL) {
      ap_entry = cJSON_CreateObject();
      cJSON_AddStringToObject(ap_entry, "bssid", bssid_str);
      cJSON_AddStringToObject(ap_entry, "ssid", ""); 
      cJSON_AddNumberToObject(ap_entry, "channel", rec->channel);
      cJSON_AddItemToObject(ap_entry, "clients", cJSON_CreateArray());
      cJSON_AddItemToArray(root, ap_entry);
    }

    cJSON *clients_array = cJSON_GetObjectItem(ap_entry, "clients");
    cJSON *client_obj = cJSON_CreateObject();
    char client_mac_str[18];
    snprintf(client_mac_str, sizeof(client_mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             rec->client_mac[0], rec->client_mac[1], rec->client_mac[2],
             rec->client_mac[3], rec->client_mac[4], rec->client_mac[5]);
    cJSON_AddStringToObject(client_obj, "mac", client_mac_str);
    cJSON_AddStringToObject(client_obj, "vendor", get_vendor_name(rec->client_mac));
    cJSON_AddNumberToObject(client_obj, "rssi", rec->rssi);
    cJSON_AddItemToArray(clients_array, client_obj);
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

bool client_scanner_save_results_to_internal_flash(void) {
  return save_results_to_path("/assets/storage/wifi/scanned_clients.json", false);
}

bool client_scanner_save_results_to_sd_card(void) {
  return save_results_to_path("/scanned_clients.json", true);
}

static void client_scanner_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting Client Scan Task (PSRAM)...");

  if (scan_results) heap_caps_free(scan_results);
  scan_results = (client_record_t *)heap_caps_malloc(max_scan_results * sizeof(client_record_t), MALLOC_CAP_SPIRAM);
  if (scan_results == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for scan results in PSRAM.");
    is_scanning = false;
    scanner_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }
  scan_count = 0;

  wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
  wifi_service_promiscuous_start(sniffer_callback, &filter);
  wifi_service_start_channel_hopping();

  ESP_LOGI(TAG, "Scanning for %d ms...", SCAN_DURATION_MS);
  vTaskDelay(pdMS_TO_TICKS(SCAN_DURATION_MS));

  wifi_service_stop_channel_hopping();
  wifi_service_promiscuous_stop();

  ESP_LOGI(TAG, "Scan completed. Found %d clients.", scan_count);

  is_scanning = false;
  scanner_task_handle = NULL;
  vTaskDelete(NULL);
}

bool client_scanner_start(void) {
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
    client_scanner_task,
    "client_scan_task",
    SCANNER_STACK_SIZE,
    NULL,
    5,
    scanner_task_stack,
    scanner_task_tcb
  );

  return (scanner_task_handle != NULL);
}

client_record_t* client_scanner_get_results(uint16_t *count) {
  if (is_scanning) {
    return NULL;
  }
  *count = scan_count;
  return scan_results;
}

void client_scanner_free_results(void) {
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
