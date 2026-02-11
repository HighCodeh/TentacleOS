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

#include "target_scanner.h"
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

static const char *TAG = "TARGET_SCANNER";

static TaskHandle_t scanner_task_handle = NULL;
static StackType_t *scanner_task_stack = NULL;
static StaticTask_t *scanner_task_tcb = NULL;
#define SCANNER_STACK_SIZE 4096
#define SCAN_DURATION_MS 30000 

static target_client_record_t *scan_results = NULL;
static uint16_t scan_count = 0;
static uint16_t max_scan_results = 200; 
static bool is_scanning = false;

static uint8_t g_target_bssid[6];
static uint8_t g_target_channel;

static void add_or_update_client(const uint8_t *client_mac, int8_t rssi) {
  if (scan_results == NULL) return;

  for (int i = 0; i < scan_count; i++) {
    if (memcmp(scan_results[i].client_mac, client_mac, 6) == 0) {
      scan_results[i].rssi = rssi; 
      return;
    }
  }

  if (scan_count < max_scan_results) {
    memcpy(scan_results[scan_count].client_mac, client_mac, 6);
    scan_results[scan_count].rssi = rssi;
    scan_count++;
    ESP_LOGI(TAG, "Target Hit: Client %02x:%02x:%02x:%02x:%02x:%02x connected to Target. RSSI: %d",
             client_mac[0], client_mac[1], client_mac[2], client_mac[3], client_mac[4], client_mac[5], rssi);
  }
}

static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_DATA) return;

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
  const wifi_mac_header_t *mac_header = (const wifi_mac_header_t *)ppkt->payload;
  const wifi_frame_control_t *fc = (const wifi_frame_control_t *)&mac_header->frame_control;

  if (fc->type != 2) return; 

  uint8_t const *bssid_in_pkt = NULL;
  uint8_t const *client_in_pkt = NULL;

  if (fc->to_ds == 1 && fc->from_ds == 0) {
    bssid_in_pkt = mac_header->addr1;
    client_in_pkt = mac_header->addr2;
  } else if (fc->to_ds == 0 && fc->from_ds == 1) {
    client_in_pkt = mac_header->addr1;
    bssid_in_pkt = mac_header->addr2;
  } else {
    return;
  }

  if (memcmp(bssid_in_pkt, g_target_bssid, 6) != 0) {
    return;
  }

  if (client_in_pkt[0] & 0x01) return;

  add_or_update_client(client_in_pkt, ppkt->rx_ctrl.rssi);
}

static bool save_results_to_path(const char *path, bool use_sd_driver) {
  if (scan_results == NULL || scan_count == 0) {
    ESP_LOGW(TAG, "No results to save.");
    return false;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) return false;

  char bssid_str[18];
  snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x",
           g_target_bssid[0], g_target_bssid[1], g_target_bssid[2],
           g_target_bssid[3], g_target_bssid[4], g_target_bssid[5]);
  cJSON_AddStringToObject(root, "target_bssid", bssid_str);
  cJSON_AddNumberToObject(root, "channel", g_target_channel);

  cJSON *clients_array = cJSON_CreateArray();
  for (int i = 0; i < scan_count; i++) {
    target_client_record_t *rec = &scan_results[i];
    cJSON *entry = cJSON_CreateObject();

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             rec->client_mac[0], rec->client_mac[1], rec->client_mac[2],
             rec->client_mac[3], rec->client_mac[4], rec->client_mac[5]);
    cJSON_AddStringToObject(entry, "mac", mac_str);
    cJSON_AddStringToObject(entry, "vendor", get_vendor_name(rec->client_mac));
    cJSON_AddNumberToObject(entry, "rssi", rec->rssi);

    cJSON_AddItemToArray(clients_array, entry);
  }
  cJSON_AddItemToObject(root, "clients", clients_array);

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
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

  ESP_LOGI(TAG, "Target scan results saved to %s", path);
  return true;
}

bool target_scanner_save_results_to_internal_flash(void) {
  return save_results_to_path("/assets/storage/wifi/target_scan.json", false);
}

bool target_scanner_save_results_to_sd_card(void) {
  return save_results_to_path("/target_scan.json", true);
}

static void target_scanner_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting Targeted Scan on Ch %d (PSRAM)...", g_target_channel);

  if (scan_results) heap_caps_free(scan_results);
  scan_results = (target_client_record_t *)heap_caps_malloc(max_scan_results * sizeof(target_client_record_t), MALLOC_CAP_SPIRAM);
  if (scan_results == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory in PSRAM.");
    is_scanning = false;
    scanner_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }
  scan_count = 0;

  esp_wifi_set_channel(g_target_channel, WIFI_SECOND_CHAN_NONE);

  wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
  wifi_service_promiscuous_start(sniffer_callback, &filter);

  vTaskDelay(pdMS_TO_TICKS(SCAN_DURATION_MS));

  wifi_service_promiscuous_stop();
  ESP_LOGI(TAG, "Target Scan completed. Found %d clients for target.", scan_count);

  is_scanning = false;
  scanner_task_handle = NULL;
  vTaskDelete(NULL);
}

bool target_scanner_start(const uint8_t *target_bssid, uint8_t channel) {
  if (is_scanning) {
    ESP_LOGW(TAG, "Scan already in progress.");
    return false;
  }
  if (target_bssid == NULL) return false;

  memcpy(g_target_bssid, target_bssid, 6);
  g_target_channel = channel;

  if (scanner_task_stack == NULL) {
    scanner_task_stack = (StackType_t *)heap_caps_malloc(SCANNER_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  }
  if (scanner_task_tcb == NULL) {
    scanner_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (scanner_task_stack == NULL || scanner_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate task memory in PSRAM!");
    if (scanner_task_stack) { heap_caps_free(scanner_task_stack); scanner_task_stack = NULL; }
    if (scanner_task_tcb) { heap_caps_free(scanner_task_tcb); scanner_task_tcb = NULL; }
    return false;
  }

  is_scanning = true;
  scanner_task_handle = xTaskCreateStatic(
    target_scanner_task,
    "tgt_scan_task",
    SCANNER_STACK_SIZE,
    NULL,
    5,
    scanner_task_stack,
    scanner_task_tcb
  );

  return (scanner_task_handle != NULL);
}

target_client_record_t* target_scanner_get_results(uint16_t *count) {
  if (is_scanning) return NULL;
  *count = scan_count;
  return scan_results;
}

target_client_record_t* target_scanner_get_live_results(uint16_t *count, bool *scanning) {
  if (scanning) *scanning = is_scanning;
  if (count) *count = scan_count;
  return scan_results;
}

void target_scanner_free_results(void) {
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
