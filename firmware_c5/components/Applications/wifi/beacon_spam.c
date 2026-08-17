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

#include "beacon_spam.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "cJSON.h"
#include "storage_impl.h"
#include "storage_read.h"

static const char *TAG = "BEACON_SPAM";

#define BEACON_SPAM_MAX_SSIDS       100
#define BEACON_SPAM_INTERVAL_MS     100
#define BEACON_SPAM_STACK_SIZE      4096
#define BEACON_SPAM_TASK_PRIORITY   SYS_PRIO_SERVICE_HI
#define BEACON_SPAM_TX_DELAY_MS     10
#define BEACON_SPAM_MAX_SSID_LEN    32
#define BEACON_SPAM_CHANNEL_COUNT   11
#define BEACON_SPAM_RANDOM_NAME_LEN 32
#define BEACON_SPAM_RANDOM_MOD      100
#define BEACON_SPAM_STOP_MARGIN_MS  50

static TaskHandle_t s_task_handle = NULL;
static StackType_t *s_task_stack = NULL;
static StaticTask_t *s_task_tcb = NULL;

static char **s_ssids = NULL;
static uint16_t s_ssids_count = 0;
static bool s_is_running = false;

static const uint8_t BEACON_TEMPLATE[] = {0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff,
                                          0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x11, 0x04};
#define BEACON_TEMPLATE_COUNT (sizeof(BEACON_TEMPLATE) / sizeof(BEACON_TEMPLATE[0]))

static void spam_task(void *pvParameters);
static void free_ssids(void);
static void free_task_memory(void);

bool beacon_spam_start_custom(const char *json_path) {
  if (s_is_running)
    return false;

  free_task_memory();

  size_t size;
  if (storage_file_get_size(json_path, &size) != ESP_OK || size == 0) {
    ESP_LOGE(TAG, "Empty or missing file: %s", json_path);
    return false;
  }

  char *json_buf = malloc(size + 1);
  if (json_buf == NULL) {
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

  if (root == NULL) {
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
  if (count > BEACON_SPAM_MAX_SSIDS)
    count = BEACON_SPAM_MAX_SSIDS;

  s_ssids = heap_caps_malloc(count * sizeof(char *), MALLOC_CAP_SPIRAM);
  if (s_ssids == NULL) {
    cJSON_Delete(root);
    return false;
  }

  s_ssids_count = 0;
  for (int i = 0; i < count; i++) {
    cJSON *item = cJSON_GetArrayItem(list, i);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
      size_t len = strlen(item->valuestring) + 1;
      s_ssids[s_ssids_count] = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
      if (s_ssids[s_ssids_count] != NULL) {
        strcpy(s_ssids[s_ssids_count], item->valuestring);
        s_ssids_count++;
      }
    }
  }
  cJSON_Delete(root);

  if (s_ssids_count == 0) {
    free_ssids();
    return false;
  }

  s_is_running = true;

  s_task_stack = (StackType_t *)heap_caps_malloc(BEACON_SPAM_STACK_SIZE * sizeof(StackType_t),
                                                 MALLOC_CAP_SPIRAM);
  s_task_tcb =
      (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (s_task_stack != NULL && s_task_tcb != NULL) {
    s_task_handle = xTaskCreateStatic(spam_task,
                                      "beacon_spam",
                                      BEACON_SPAM_STACK_SIZE,
                                      NULL,
                                      BEACON_SPAM_TASK_PRIORITY,
                                      s_task_stack,
                                      s_task_tcb);
  }

  if (s_task_handle == NULL) {
    ESP_LOGE(TAG, "Failed to create task in PSRAM");
    beacon_spam_stop();
    return false;
  }

  ESP_LOGI(TAG, "Custom Beacon Spam started with %d SSIDs", s_ssids_count);
  return true;
}

bool beacon_spam_start_random(void) {
  if (s_is_running)
    return false;

  free_task_memory();

  s_ssids_count = BEACON_SPAM_MAX_SSIDS;
  s_ssids = heap_caps_malloc(s_ssids_count * sizeof(char *), MALLOC_CAP_SPIRAM);
  if (s_ssids == NULL)
    return false;

  for (int i = 0; i < s_ssids_count; i++) {
    s_ssids[i] = heap_caps_malloc(BEACON_SPAM_RANDOM_NAME_LEN, MALLOC_CAP_SPIRAM);
    if (s_ssids[i] != NULL) {
      if (i % 2 == 0) {
        snprintf(s_ssids[i],
                 BEACON_SPAM_RANDOM_NAME_LEN,
                 "WiFi-%04X",
                 (unsigned int)esp_random() & 0xFFFF);
      } else {
        snprintf(s_ssids[i],
                 BEACON_SPAM_RANDOM_NAME_LEN,
                 "AP_%d_%02d",
                 (int)(esp_random() % BEACON_SPAM_RANDOM_MOD),
                 i);
      }
    }
  }

  s_is_running = true;

  s_task_stack = (StackType_t *)heap_caps_malloc(BEACON_SPAM_STACK_SIZE * sizeof(StackType_t),
                                                 MALLOC_CAP_SPIRAM);
  s_task_tcb =
      (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (s_task_stack != NULL && s_task_tcb != NULL) {
    s_task_handle = xTaskCreateStatic(spam_task,
                                      "beacon_spam",
                                      BEACON_SPAM_STACK_SIZE,
                                      NULL,
                                      BEACON_SPAM_TASK_PRIORITY,
                                      s_task_stack,
                                      s_task_tcb);
  }

  if (s_task_handle == NULL) {
    ESP_LOGE(TAG, "Failed to create task in PSRAM");
    beacon_spam_stop();
    return false;
  }

  ESP_LOGI(TAG, "Random Beacon Spam started with %d SSIDs", s_ssids_count);
  return true;
}

void beacon_spam_stop(void) {
  if (!s_is_running)
    return;

  s_is_running = false;
  if (s_task_handle != NULL) {
    vTaskDelay(pdMS_TO_TICKS(BEACON_SPAM_INTERVAL_MS + BEACON_SPAM_STOP_MARGIN_MS));
  }

  free_task_memory();
  free_ssids();
  ESP_LOGI(TAG, "Beacon Spam stopped");
}

bool beacon_spam_is_running(void) {
  return s_is_running;
}

static void spam_task(void *pvParameters) {
  uint8_t packet[128];
  uint8_t mac[6];

  esp_fill_random(mac, 6);
  mac[0] &= 0xFC;
  uint8_t base_last_byte = mac[5];

  while (s_is_running) {
    for (int i = 0; i < s_ssids_count; i++) {
      if (!s_is_running)
        break;

      int idx = 0;
      memcpy(packet, BEACON_TEMPLATE, sizeof(BEACON_TEMPLATE));
      idx += sizeof(BEACON_TEMPLATE);

      mac[5] = (uint8_t)(base_last_byte + i);

      memcpy(&packet[10], mac, 6);
      memcpy(&packet[16], mac, 6);

      packet[idx++] = 0x00;
      uint8_t len = strlen(s_ssids[i]);
      if (len > BEACON_SPAM_MAX_SSID_LEN)
        len = BEACON_SPAM_MAX_SSID_LEN;
      packet[idx++] = len;
      memcpy(&packet[idx], s_ssids[i], len);
      idx += len;

      uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
      packet[idx++] = 0x01;
      packet[idx++] = sizeof(rates);
      memcpy(&packet[idx], rates, sizeof(rates));
      idx += sizeof(rates);

      packet[idx++] = 0x03;
      packet[idx++] = 0x01;
      uint8_t chan = (i % BEACON_SPAM_CHANNEL_COUNT) + 1;
      packet[idx++] = chan;

      esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
      esp_wifi_80211_tx(WIFI_IF_AP, packet, idx, false);

      vTaskDelay(pdMS_TO_TICKS(BEACON_SPAM_TX_DELAY_MS));
    }
    vTaskDelay(pdMS_TO_TICKS(BEACON_SPAM_INTERVAL_MS));
  }

  vTaskDelete(NULL);
  s_task_handle = NULL;
}

static void free_ssids(void) {
  if (s_ssids != NULL) {
    for (int i = 0; i < s_ssids_count; i++) {
      if (s_ssids[i] != NULL)
        free(s_ssids[i]);
    }
    free(s_ssids);
    s_ssids = NULL;
  }
  s_ssids_count = 0;
}

static void free_task_memory(void) {
  if (s_task_stack != NULL) {
    free(s_task_stack);
    s_task_stack = NULL;
  }
  if (s_task_tcb != NULL) {
    free(s_task_tcb);
    s_task_tcb = NULL;
  }
}
