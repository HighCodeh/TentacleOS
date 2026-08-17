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

#include "target_scanner.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "cJSON.h"
#include "mac_vendor.h"
#include "sd_card_init.h"
#include "sd_card_write.h"
#include "storage_write.h"
#include "tos_flash_paths.h"
#include "wifi_80211.h"
#include "wifi_service.h"

static const char *TAG = "TARGET_SCANNER";

#define SCANNER_STACK_SIZE    4096
#define SCANNER_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define SCAN_DURATION_MS      30000
#define MAX_SCAN_RESULTS      200
#define BSSID_LEN             6
#define DATA_FRAME_TYPE       2

static TaskHandle_t s_scanner_task_handle = NULL;
static StackType_t *s_scanner_task_stack = NULL;
static StaticTask_t *s_scanner_task_tcb = NULL;

static target_scanner_record_t *s_scan_results = NULL;
static uint16_t s_scan_count = 0;
static bool s_is_scanning = false;

static uint8_t s_target_bssid[BSSID_LEN];
static uint8_t s_target_channel;

static void add_or_update_client(const uint8_t *client_mac, int8_t rssi);
static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type);
static bool save_results_to_path(const char *path, bool use_sd_driver);
static void scanner_task(void *pvParameters);

bool target_scanner_start(const uint8_t *target_bssid, uint8_t channel) {
  if (s_is_scanning) {
    ESP_LOGW(TAG, "Scan already in progress.");
    return false;
  }
  if (target_bssid == NULL)
    return false;

  memcpy(s_target_bssid, target_bssid, BSSID_LEN);
  s_target_channel = channel;

  if (s_scanner_task_stack == NULL) {
    s_scanner_task_stack = (StackType_t *)heap_caps_malloc(SCANNER_STACK_SIZE * sizeof(StackType_t),
                                                           MALLOC_CAP_SPIRAM);
  }
  if (s_scanner_task_tcb == NULL) {
    s_scanner_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t),
                                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (s_scanner_task_stack == NULL || s_scanner_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate task memory in PSRAM!");
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
                                            "tgt_scan_task",
                                            SCANNER_STACK_SIZE,
                                            NULL,
                                            SCANNER_TASK_PRIORITY,
                                            s_scanner_task_stack,
                                            s_scanner_task_tcb);

  return (s_scanner_task_handle != NULL);
}

target_scanner_record_t *target_scanner_get_results(uint16_t *out_count) {
  if (s_is_scanning)
    return NULL;
  *out_count = s_scan_count;
  return s_scan_results;
}

target_scanner_record_t *target_scanner_get_live_results(uint16_t *out_count, bool *out_scanning) {
  if (out_scanning != NULL)
    *out_scanning = s_is_scanning;
  if (out_count != NULL)
    *out_count = s_scan_count;
  return s_scan_results;
}

const uint16_t *target_scanner_get_count_ptr(void) {
  return &s_scan_count;
}

bool target_scanner_is_scanning(void) {
  return s_is_scanning;
}

void target_scanner_free_results(void) {
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

bool target_scanner_save_results_to_internal_flash(void) {
  return save_results_to_path(FLASH_STORAGE_WIFI_TARGETS, false);
}

bool target_scanner_save_results_to_sd_card(void) {
  return save_results_to_path("/target_scan.json", true);
}

static void add_or_update_client(const uint8_t *client_mac, int8_t rssi) {
  if (s_scan_results == NULL)
    return;

  for (int i = 0; i < s_scan_count; i++) {
    if (memcmp(s_scan_results[i].client_mac, client_mac, BSSID_LEN) == 0) {
      s_scan_results[i].rssi = rssi;
      return;
    }
  }

  if (s_scan_count < MAX_SCAN_RESULTS) {
    memcpy(s_scan_results[s_scan_count].client_mac, client_mac, BSSID_LEN);
    s_scan_results[s_scan_count].rssi = rssi;
    s_scan_count++;
    ESP_LOGI(TAG,
             "Target Hit: Client %02x:%02x:%02x:%02x:%02x:%02x connected to Target. RSSI: %d",
             client_mac[0],
             client_mac[1],
             client_mac[2],
             client_mac[3],
             client_mac[4],
             client_mac[5],
             rssi);
  }
}

static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_DATA)
    return;

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
  const wifi_mac_header_t *mac_header = (const wifi_mac_header_t *)ppkt->payload;
  const wifi_frame_control_t *fc = (const wifi_frame_control_t *)&mac_header->frame_control;

  if (fc->type != DATA_FRAME_TYPE)
    return;

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

  if (memcmp(bssid_in_pkt, s_target_bssid, BSSID_LEN) != 0) {
    return;
  }

  if (client_in_pkt[0] & 0x01)
    return;

  add_or_update_client(client_in_pkt, ppkt->rx_ctrl.rssi);
}

static bool save_results_to_path(const char *path, bool use_sd_driver) {
  if (s_scan_results == NULL || s_scan_count == 0) {
    ESP_LOGW(TAG, "No results to save.");
    return false;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return false;

  char bssid_str[18];
  snprintf(bssid_str,
           sizeof(bssid_str),
           "%02x:%02x:%02x:%02x:%02x:%02x",
           s_target_bssid[0],
           s_target_bssid[1],
           s_target_bssid[2],
           s_target_bssid[3],
           s_target_bssid[4],
           s_target_bssid[5]);
  cJSON_AddStringToObject(root, "target_bssid", bssid_str);
  cJSON_AddNumberToObject(root, "channel", s_target_channel);

  cJSON *clients_array = cJSON_CreateArray();
  for (int i = 0; i < s_scan_count; i++) {
    target_scanner_record_t *rec = &s_scan_results[i];
    cJSON *entry = cJSON_CreateObject();

    char mac_str[18];
    snprintf(mac_str,
             sizeof(mac_str),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             rec->client_mac[0],
             rec->client_mac[1],
             rec->client_mac[2],
             rec->client_mac[3],
             rec->client_mac[4],
             rec->client_mac[5]);
    cJSON_AddStringToObject(entry, "mac", mac_str);
    cJSON_AddStringToObject(entry, "vendor", mac_vendor_get_name(rec->client_mac));
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

static void scanner_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting Targeted Scan on Ch %d (PSRAM)...", s_target_channel);

  if (s_scan_results != NULL)
    free(s_scan_results);
  s_scan_results = (target_scanner_record_t *)heap_caps_malloc(
      MAX_SCAN_RESULTS * sizeof(target_scanner_record_t), MALLOC_CAP_SPIRAM);
  if (s_scan_results == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory in PSRAM.");
    s_is_scanning = false;
    s_scanner_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }
  s_scan_count = 0;

  esp_wifi_set_channel(s_target_channel, WIFI_SECOND_CHAN_NONE);

  wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA};
  wifi_service_promiscuous_start(sniffer_callback, &filter);

  vTaskDelay(pdMS_TO_TICKS(SCAN_DURATION_MS));

  wifi_service_promiscuous_stop();
  ESP_LOGI(TAG, "Target Scan completed. Found %d clients for target.", s_scan_count);

  s_is_scanning = false;
  s_scanner_task_handle = NULL;
  vTaskDelete(NULL);
}
