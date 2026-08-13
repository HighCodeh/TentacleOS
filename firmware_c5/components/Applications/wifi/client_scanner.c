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

#include "client_scanner.h"

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

static const char *TAG = "CLIENT_SCANNER";

#define SCANNER_STACK_SIZE    4096
#define SCANNER_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define SCAN_DURATION_MS      15000
#define MAX_SCAN_RESULTS      200
#define BSSID_LEN             6
#define DATA_FRAME_TYPE       2

static TaskHandle_t s_scanner_task_handle = NULL;
static StackType_t *s_scanner_task_stack = NULL;
static StaticTask_t *s_scanner_task_tcb = NULL;

static client_scanner_record_t *s_scan_results = NULL;
static uint16_t s_scan_count = 0;
static bool s_is_scanning = false;

static void
add_or_update_client(const uint8_t *bssid, const uint8_t *client_mac, int8_t rssi, uint8_t channel);
static void sniffer_callback(void *buf, wifi_promiscuous_pkt_type_t type);
static bool save_results_to_path(const char *path, bool use_sd_driver);
static void scanner_task(void *pvParameters);

bool client_scanner_start(void) {
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
                                            "client_scan_task",
                                            SCANNER_STACK_SIZE,
                                            NULL,
                                            SCANNER_TASK_PRIORITY,
                                            s_scanner_task_stack,
                                            s_scanner_task_tcb);

  return (s_scanner_task_handle != NULL);
}

client_scanner_record_t *client_scanner_get_results(uint16_t *out_count) {
  if (s_is_scanning) {
    return NULL;
  }
  *out_count = s_scan_count;
  return s_scan_results;
}

void client_scanner_free_results(void) {
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

bool client_scanner_save_results_to_internal_flash(void) {
  return save_results_to_path(FLASH_STORAGE_WIFI_CLIENTS, false);
}

bool client_scanner_save_results_to_sd_card(void) {
  return save_results_to_path("/scanned_clients.json", true);
}

static void add_or_update_client(const uint8_t *bssid,
                                 const uint8_t *client_mac,
                                 int8_t rssi,
                                 uint8_t channel) {
  if (s_scan_results == NULL)
    return;

  for (int i = 0; i < s_scan_count; i++) {
    if (memcmp(s_scan_results[i].client_mac, client_mac, BSSID_LEN) == 0) {
      s_scan_results[i].rssi = rssi;
      s_scan_results[i].channel = channel;
      memcpy(s_scan_results[i].bssid, bssid, BSSID_LEN);
      return;
    }
  }

  if (s_scan_count < MAX_SCAN_RESULTS) {
    memcpy(s_scan_results[s_scan_count].bssid, bssid, BSSID_LEN);
    memcpy(s_scan_results[s_scan_count].client_mac, client_mac, BSSID_LEN);
    s_scan_results[s_scan_count].rssi = rssi;
    s_scan_results[s_scan_count].channel = channel;
    s_scan_count++;
    ESP_LOGI(
        TAG,
        "New Client: %02x:%02x:%02x:%02x:%02x:%02x (BSSID: %02x:%02x:%02x:%02x:%02x:%02x) RSSI: %d",
        client_mac[0],
        client_mac[1],
        client_mac[2],
        client_mac[3],
        client_mac[4],
        client_mac[5],
        bssid[0],
        bssid[1],
        bssid[2],
        bssid[3],
        bssid[4],
        bssid[5],
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

  if (client[0] & 0x01)
    return;

  uint8_t channel = ppkt->rx_ctrl.channel;

  add_or_update_client(bssid, client, ppkt->rx_ctrl.rssi, channel);
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
    client_scanner_record_t *rec = &s_scan_results[i];

    cJSON *ap_entry = NULL;
    char bssid_str[18];
    snprintf(bssid_str,
             sizeof(bssid_str),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             rec->bssid[0],
             rec->bssid[1],
             rec->bssid[2],
             rec->bssid[3],
             rec->bssid[4],
             rec->bssid[5]);

    int array_size = cJSON_GetArraySize(root);
    for (int j = 0; j < array_size; j++) {
      cJSON *item = cJSON_GetArrayItem(root, j);
      cJSON *bssid_obj = cJSON_GetObjectItem(item, "bssid");
      if (bssid_obj != NULL && strcmp(bssid_obj->valuestring, bssid_str) == 0) {
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
    snprintf(client_mac_str,
             sizeof(client_mac_str),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             rec->client_mac[0],
             rec->client_mac[1],
             rec->client_mac[2],
             rec->client_mac[3],
             rec->client_mac[4],
             rec->client_mac[5]);
    cJSON_AddStringToObject(client_obj, "mac", client_mac_str);
    cJSON_AddStringToObject(client_obj, "vendor", mac_vendor_get_name(rec->client_mac));
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

static void scanner_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting Client Scan Task (PSRAM)...");

  if (s_scan_results != NULL)
    free(s_scan_results);
  s_scan_results = (client_scanner_record_t *)heap_caps_malloc(
      MAX_SCAN_RESULTS * sizeof(client_scanner_record_t), MALLOC_CAP_SPIRAM);
  if (s_scan_results == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for scan results in PSRAM.");
    s_is_scanning = false;
    s_scanner_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }
  s_scan_count = 0;

  wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA};
  wifi_service_promiscuous_start(sniffer_callback, &filter);
  wifi_service_start_channel_hopping();

  ESP_LOGI(TAG, "Scanning for %d ms...", SCAN_DURATION_MS);
  vTaskDelay(pdMS_TO_TICKS(SCAN_DURATION_MS));

  wifi_service_stop_channel_hopping();
  wifi_service_promiscuous_stop();

  ESP_LOGI(TAG, "Scan completed. Found %d clients.", s_scan_count);

  s_is_scanning = false;
  s_scanner_task_handle = NULL;
  vTaskDelete(NULL);
}
