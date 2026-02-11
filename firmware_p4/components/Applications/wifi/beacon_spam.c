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

#include "beacon_spam.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "storage_read.h"
#include "storage_impl.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "BEACON_SPAM";

#define BEACON_SPAM_MAX_SSIDS 100
#define BEACON_SPAM_INTERVAL_MS 100
#define BEACON_SPAM_STACK_SIZE 4096

static TaskHandle_t spam_task_handle = NULL;
static StackType_t *spam_task_stack = NULL;
static StaticTask_t *spam_task_tcb = NULL;

static char **ssids = NULL;
static uint16_t ssids_count = 0;
static bool is_running = false;

static const uint8_t beacon_template[] = {
  0x80, 0x00,                         
  0x00, 0x00,                         
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00,                         
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x64, 0x00,                         
  0x11, 0x04                          
};

static void beacon_spam_task(void *pvParameters) {
  uint8_t packet[128];
  uint8_t mac[6];

  esp_fill_random(mac, 6);
  mac[0] &= 0xFC; 
  uint8_t base_last_byte = mac[5];

  while (is_running) {
    for (int i = 0; i < ssids_count; i++) {
      if (!is_running) break;

      int idx = 0;
      memcpy(packet, beacon_template, sizeof(beacon_template));
      idx += sizeof(beacon_template);

      mac[5] = (uint8_t)(base_last_byte + i);

      memcpy(&packet[10], mac, 6);
      memcpy(&packet[16], mac, 6);

      packet[idx++] = 0x00;
      uint8_t len = strlen(ssids[i]);
      if (len > 32) len = 32;
      packet[idx++] = len;
      memcpy(&packet[idx], ssids[i], len);
      idx += len;

      uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
      packet[idx++] = 0x01;
      packet[idx++] = sizeof(rates);
      memcpy(&packet[idx], rates, sizeof(rates));
      idx += sizeof(rates);

      packet[idx++] = 0x03;
      packet[idx++] = 0x01;
      uint8_t chan = (i % 11) + 1; 
      packet[idx++] = chan;

      esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
      esp_wifi_80211_tx(WIFI_IF_AP, packet, idx, false);

      vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(BEACON_SPAM_INTERVAL_MS));
  }

  vTaskDelete(NULL);
  spam_task_handle = NULL;
}

static void free_ssids() {
  if (ssids) {
    for (int i = 0; i < ssids_count; i++) {
      if (ssids[i]) heap_caps_free(ssids[i]);
    }
    heap_caps_free(ssids);
    ssids = NULL;
  }
  ssids_count = 0;
}

static void free_task_memory() {
  if (spam_task_stack) { heap_caps_free(spam_task_stack); spam_task_stack = NULL; }
  if (spam_task_tcb) { heap_caps_free(spam_task_tcb); spam_task_tcb = NULL; }
}

bool beacon_spam_start_custom(const char *json_path) {
  if (is_running) return false;
  
  free_task_memory(); // Ensure clean start

  size_t size;
  if (storage_file_get_size(json_path, &size) != ESP_OK || size == 0) {
    ESP_LOGE(TAG, "Empty or missing file: %s", json_path);
    return false;
  }

  char *json_buf = malloc(size + 1);
  if (!json_buf) {
    ESP_LOGE(TAG, "Failed to allocate buffer");
    return false;
  }

  if (storage_read_string(json_path, json_buf, size + 1) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read file");
    free(json_buf);
    return false;
  }

  cJSON *root = cJSON_Parse(json_buf);
  free(json_buf);

  if (!root) {
    ESP_LOGE(TAG, "Failed to parse JSON");
    return false;
  }

  cJSON *list = cJSON_GetObjectItem(root, "spam_networks"); 

  if (!cJSON_IsArray(list)) {
    ESP_LOGE(TAG, "JSON does not contain array");
    cJSON_Delete(root);
    return false;
  }

  int count = cJSON_GetArraySize(list);
  if (count > BEACON_SPAM_MAX_SSIDS) count = BEACON_SPAM_MAX_SSIDS;

  ssids = heap_caps_malloc(count * sizeof(char *), MALLOC_CAP_SPIRAM);
  if (!ssids) {
    cJSON_Delete(root);
    return false;
  }

  ssids_count = 0;
  for (int i = 0; i < count; i++) {
    cJSON *item = cJSON_GetArrayItem(list, i);
    if (cJSON_IsString(item) && item->valuestring) {
      size_t len = strlen(item->valuestring) + 1;
      ssids[ssids_count] = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
      if (ssids[ssids_count]) {
        strcpy(ssids[ssids_count], item->valuestring);
        ssids_count++;
      }
    }
  }
  cJSON_Delete(root);

  if (ssids_count == 0) {
    free_ssids();
    return false;
  }

  is_running = true;

  spam_task_stack = (StackType_t *)heap_caps_malloc(BEACON_SPAM_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  spam_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (spam_task_stack && spam_task_tcb) {
    spam_task_handle = xTaskCreateStatic(beacon_spam_task, "beacon_spam", BEACON_SPAM_STACK_SIZE, NULL, 5, spam_task_stack, spam_task_tcb);
  }

  if (!spam_task_handle) {
    ESP_LOGE(TAG, "Failed to create task in PSRAM");
    beacon_spam_stop();
    return false;
  }

  ESP_LOGI(TAG, "Custom Beacon Spam started with %d SSIDs", ssids_count);
  return true;
}

bool beacon_spam_start_random(void) {
  if (is_running) return false;

  free_task_memory(); // Ensure clean start

  ssids_count = BEACON_SPAM_MAX_SSIDS;
  ssids = heap_caps_malloc(ssids_count * sizeof(char *), MALLOC_CAP_SPIRAM);
  if (!ssids) return false;

  for (int i = 0; i < ssids_count; i++) {
    ssids[i] = heap_caps_malloc(32, MALLOC_CAP_SPIRAM);
    if (ssids[i]) {
      if (i % 2 == 0) {
        snprintf(ssids[i], 32, "WiFi-%04X", (unsigned int)esp_random() & 0xFFFF);
      } else {
        snprintf(ssids[i], 32, "AP_%d_%02d", (int)(esp_random() % 100), i);
      }
    }
  }

  is_running = true;

  spam_task_stack = (StackType_t *)heap_caps_malloc(BEACON_SPAM_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  spam_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (spam_task_stack && spam_task_tcb) {
    spam_task_handle = xTaskCreateStatic(beacon_spam_task, "beacon_spam", BEACON_SPAM_STACK_SIZE, NULL, 5, spam_task_stack, spam_task_tcb);
  }

  if (!spam_task_handle) {
    ESP_LOGE(TAG, "Failed to create task in PSRAM");
    beacon_spam_stop();
    return false;
  }

  ESP_LOGI(TAG, "Random Beacon Spam started with %d SSIDs", ssids_count);
  return true;
}

void beacon_spam_stop(void) {
  if (!is_running) return;

  is_running = false;
  if (spam_task_handle) {
    vTaskDelay(pdMS_TO_TICKS(BEACON_SPAM_INTERVAL_MS + 50));
  }
  
  // We can free task memory here if we waited enough
  free_task_memory();

  free_ssids();
  ESP_LOGI(TAG, "Beacon Spam stopped");
}

bool beacon_spam_is_running(void) {
  return is_running;
}
