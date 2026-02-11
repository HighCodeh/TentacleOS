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

#include "probe_monitor.h"
#include "wifi_service.h"
#include "wifi_80211.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "cJSON.h"
#include "storage_write.h"
#include "sd_card_write.h"
#include "sd_card_init.h"
#include "mac_vendor.h"

static const char *TAG = "PROBE_MONITOR";

static TaskHandle_t monitor_task_handle = NULL;
static StackType_t *monitor_task_stack = NULL;
static StaticTask_t *monitor_task_tcb = NULL;
#define MONITOR_STACK_SIZE 4096

static probe_record_t *scan_results = NULL;
static uint16_t scan_count = 0;
static uint16_t max_scan_results = 300; 
static bool is_running = false;

static void add_or_update_probe(const uint8_t *mac, const char *ssid, int8_t rssi) {
  if (scan_results == NULL) return;

  for (int i = 0; i < scan_count; i++) {
    if (memcmp(scan_results[i].mac, mac, 6) == 0 && strcmp(scan_results[i].ssid, ssid) == 0) {
      scan_results[i].rssi = rssi;
      scan_results[i].last_seen_timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
      return;
    }
  }

  if (scan_count < max_scan_results) {
    memcpy(scan_results[scan_count].mac, mac, 6);
    strncpy(scan_results[scan_count].ssid, ssid, 32);
    scan_results[scan_count].ssid[32] = '\0';
    scan_results[scan_count].rssi = rssi;
    scan_results[scan_count].last_seen_timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
    scan_count++;

    ESP_LOGI(TAG, "New Probe: [%02x:%02x:%02x:%02x:%02x:%02x] searching for '%s' (RSSI: %d)",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ssid, rssi);
  }
}

static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
  const wifi_mac_header_t *mac_header = (const wifi_mac_header_t *)ppkt->payload;
  const wifi_frame_control_t *fc = (const wifi_frame_control_t *)&mac_header->frame_control;

  if (fc->type == 0 && fc->subtype == 4) {
    const uint8_t *payload = ppkt->payload + 24;
    int payload_len = ppkt->rx_ctrl.sig_len - 24;

    if (payload_len < 2) return;

    int offset = 0;
    char ssid[33] = {0};
    bool ssid_found = false;

    while (offset + 2 <= payload_len) {
      uint8_t tag = payload[offset];
      uint8_t len = payload[offset + 1];

      if (offset + 2 + len > payload_len) break;

      if (tag == 0) { 
        int ssid_len = (len > 32) ? 32 : len;
        memcpy(ssid, &payload[offset + 2], ssid_len);
        ssid[ssid_len] = '\0';
        ssid_found = true;
        break; 
      }
      offset += 2 + len;
    }

    if (ssid_found) {
      if (strlen(ssid) == 0) {
        strcpy(ssid, "<Broadcast>");
      }
      add_or_update_probe(mac_header->addr2, ssid, ppkt->rx_ctrl.rssi);
    }
  }
}

static bool save_results_to_path(const char *path, bool use_sd_driver) {
  if (scan_results == NULL || scan_count == 0) {
    ESP_LOGW(TAG, "No results to save.");
    return false;
  }

  cJSON *root = cJSON_CreateArray();
  for (int i = 0; i < scan_count; i++) {
    probe_record_t *rec = &scan_results[i];
    cJSON *entry = cJSON_CreateObject();

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             rec->mac[0], rec->mac[1], rec->mac[2],
             rec->mac[3], rec->mac[4], rec->mac[5]);
    cJSON_AddStringToObject(entry, "mac", mac_str);
    cJSON_AddStringToObject(entry, "vendor", get_vendor_name(rec->mac));
    cJSON_AddStringToObject(entry, "ssid", rec->ssid);
    cJSON_AddNumberToObject(entry, "rssi", rec->rssi);
    cJSON_AddNumberToObject(entry, "timestamp", rec->last_seen_timestamp);

    cJSON_AddItemToArray(root, entry);
  }

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
    cJSON_Delete(root);
    return false;
  }

  esp_err_t err;
  if (use_sd_driver) {
    if (!sd_is_mounted()) {
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
    ESP_LOGE(TAG, "Failed to save results to %s", path);
    return false;
  }

  ESP_LOGI(TAG, "Results saved to %s", path);
  return true;
}

bool probe_monitor_start(void) {
  if (is_running) return false;

  if (scan_results) heap_caps_free(scan_results);
  scan_results = (probe_record_t *)heap_caps_malloc(max_scan_results * sizeof(probe_record_t), MALLOC_CAP_SPIRAM);
  if (scan_results == NULL) return false;

  scan_count = 0;
  is_running = true;

  wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
  wifi_service_promiscuous_start(sniffer_callback, &filter);
  wifi_service_start_channel_hopping();

  ESP_LOGI(TAG, "Probe Request Monitor started.");
  return true;
}

void probe_monitor_stop(void) {
  if (!is_running) return;
  wifi_service_promiscuous_stop();
  wifi_service_stop_channel_hopping();
  is_running = false;
  ESP_LOGI(TAG, "Probe Request Monitor stopped.");
}

probe_record_t* probe_monitor_get_results(uint16_t *count) {
  *count = scan_count;
  return scan_results;
}

void probe_monitor_free_results(void) {
  probe_monitor_stop();
  if (scan_results) {
    heap_caps_free(scan_results);
    scan_results = NULL;
  }
  scan_count = 0;
}

bool probe_monitor_save_results_to_internal_flash(void) {
  return save_results_to_path("/assets/storage/wifi/probe_monitor.json", false);
}

bool probe_monitor_save_results_to_sd_card(void) {
  return save_results_to_path("/probe_monitor.json", true);
}
