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


#include "signal_monitor.h"
#include "wifi_service.h"
#include "wifi_80211.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "SIGNAL_MONITOR";

static uint8_t target_bssid[6] = {0};
static int8_t last_rssi = -127;
static int64_t last_seen_time = 0;
static bool is_running = false;

// Task Handles
static TaskHandle_t monitor_task_handle = NULL;
static StackType_t *monitor_task_stack = NULL;
static StaticTask_t *monitor_task_tcb = NULL;
#define MONITOR_STACK_SIZE 4096
#define SIGNAL_TIMEOUT_MS 5000 // Reset RSSI if no packet for 5s

static void monitor_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!is_running) return;

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
  const wifi_mac_header_t *mac_header = (const wifi_mac_header_t *)ppkt->payload;

  if (memcmp(mac_header->addr2, target_bssid, 6) == 0) {
    last_rssi = ppkt->rx_ctrl.rssi;
    last_seen_time = esp_timer_get_time();
  }
}

static void signal_monitor_task(void *pvParameters) {
  while (is_running) {
    if (last_seen_time > 0) {
      int64_t now = esp_timer_get_time();
      int64_t diff_ms = (now - last_seen_time) / 1000;

      // Signal Lost Logic
      if (diff_ms > SIGNAL_TIMEOUT_MS) {
        last_rssi = -127; 
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500)); // Check every 500ms
  }
  vTaskDelete(NULL);
}

void signal_monitor_start(const uint8_t *bssid, uint8_t channel) {
  if (is_running) {
    signal_monitor_stop();
  }

  if (bssid == NULL) return;

  memcpy(target_bssid, bssid, 6);
  last_rssi = -127;
  last_seen_time = 0;
  is_running = true;

  ESP_LOGI(TAG, "Starting Signal Monitor for Target: %02x:%02x:%02x:%02x:%02x:%02x on Ch %d",
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  wifi_promiscuous_filter_t filter = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
  };

  wifi_service_promiscuous_start(monitor_callback, &filter);

  if (monitor_task_stack == NULL) {
    monitor_task_stack = (StackType_t *)heap_caps_malloc(MONITOR_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  }
  if (monitor_task_tcb == NULL) {
    monitor_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (monitor_task_stack == NULL || monitor_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate monitor task memory in PSRAM!");
    if (monitor_task_stack) { heap_caps_free(monitor_task_stack); monitor_task_stack = NULL; }
    if (monitor_task_tcb) { heap_caps_free(monitor_task_tcb); monitor_task_tcb = NULL; }
    return;
  }

  monitor_task_handle = xTaskCreateStatic(
    signal_monitor_task,
    "sig_mon_task",
    MONITOR_STACK_SIZE,
    NULL,
    5,
    monitor_task_stack,
    monitor_task_tcb
  );
}

void signal_monitor_stop(void) {
  if (!is_running) return;

  is_running = false;

  wifi_service_promiscuous_stop();

  // Give task time to exit
  vTaskDelay(pdMS_TO_TICKS(600)); 

  monitor_task_handle = NULL;

  if (monitor_task_stack) {
    heap_caps_free(monitor_task_stack);
    monitor_task_stack = NULL;
  }
  if (monitor_task_tcb) {
    heap_caps_free(monitor_task_tcb);
    monitor_task_tcb = NULL;
  }

  memset(target_bssid, 0, 6);
  ESP_LOGI(TAG, "Signal Monitor Stopped");
}

int8_t signal_monitor_get_rssi(void) {
  return last_rssi;
}

uint32_t signal_monitor_get_last_seen_ms(void) {
  if (last_seen_time == 0) return UINT32_MAX;

  int64_t now = esp_timer_get_time();
  return (uint32_t)((now - last_seen_time) / 1000);
}
