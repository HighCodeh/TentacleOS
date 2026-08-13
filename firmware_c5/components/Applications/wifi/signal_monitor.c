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

#include "signal_monitor.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "wifi_80211.h"
#include "wifi_service.h"

static const char *TAG = "SIGNAL_MONITOR";

#define MONITOR_STACK_SIZE       4096
#define MONITOR_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define SIGNAL_TIMEOUT_MS        5000
#define SIGNAL_CHECK_INTERVAL_MS 500
#define SIGNAL_STOP_WAIT_MS      600
#define RSSI_NO_SIGNAL           (-127)
#define BSSID_LEN                6
#define US_PER_MS                1000

static uint8_t s_target_bssid[BSSID_LEN] = {0};
static int8_t s_last_rssi = RSSI_NO_SIGNAL;
static int64_t s_last_seen_time = 0;
static bool s_is_running = false;

static TaskHandle_t s_monitor_task_handle = NULL;
static StackType_t *s_monitor_task_stack = NULL;
static StaticTask_t *s_monitor_task_tcb = NULL;

static void monitor_callback(void *buf, wifi_promiscuous_pkt_type_t type);
static void monitor_task(void *pvParameters);

void signal_monitor_start(const uint8_t *bssid, uint8_t channel) {
  if (s_is_running) {
    signal_monitor_stop();
  }

  if (bssid == NULL)
    return;

  memcpy(s_target_bssid, bssid, BSSID_LEN);
  s_last_rssi = RSSI_NO_SIGNAL;
  s_last_seen_time = 0;
  s_is_running = true;

  ESP_LOGI(TAG,
           "Starting Signal Monitor for Target: %02x:%02x:%02x:%02x:%02x:%02x on Ch %d",
           bssid[0],
           bssid[1],
           bssid[2],
           bssid[3],
           bssid[4],
           bssid[5],
           channel);

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                                                     WIFI_PROMIS_FILTER_MASK_DATA};

  wifi_service_promiscuous_start(monitor_callback, &filter);

  if (s_monitor_task_stack == NULL) {
    s_monitor_task_stack = (StackType_t *)heap_caps_malloc(MONITOR_STACK_SIZE * sizeof(StackType_t),
                                                           MALLOC_CAP_SPIRAM);
  }
  if (s_monitor_task_tcb == NULL) {
    s_monitor_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t),
                                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (s_monitor_task_stack == NULL || s_monitor_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to allocate monitor task memory in PSRAM!");
    if (s_monitor_task_stack != NULL) {
      free(s_monitor_task_stack);
      s_monitor_task_stack = NULL;
    }
    if (s_monitor_task_tcb != NULL) {
      free(s_monitor_task_tcb);
      s_monitor_task_tcb = NULL;
    }
    return;
  }

  s_monitor_task_handle = xTaskCreateStatic(monitor_task,
                                            "sig_mon_task",
                                            MONITOR_STACK_SIZE,
                                            NULL,
                                            MONITOR_TASK_PRIORITY,
                                            s_monitor_task_stack,
                                            s_monitor_task_tcb);
}

void signal_monitor_stop(void) {
  if (!s_is_running)
    return;

  s_is_running = false;

  wifi_service_promiscuous_stop();

  // Give task time to exit
  vTaskDelay(pdMS_TO_TICKS(SIGNAL_STOP_WAIT_MS));

  s_monitor_task_handle = NULL;

  if (s_monitor_task_stack != NULL) {
    free(s_monitor_task_stack);
    s_monitor_task_stack = NULL;
  }
  if (s_monitor_task_tcb != NULL) {
    free(s_monitor_task_tcb);
    s_monitor_task_tcb = NULL;
  }

  memset(s_target_bssid, 0, BSSID_LEN);
  ESP_LOGI(TAG, "Signal Monitor Stopped");
}

int8_t signal_monitor_get_rssi(void) {
  return s_last_rssi;
}

uint32_t signal_monitor_get_last_seen_ms(void) {
  if (s_last_seen_time == 0)
    return UINT32_MAX;

  int64_t now = esp_timer_get_time();
  return (uint32_t)((now - s_last_seen_time) / US_PER_MS);
}

static void monitor_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!s_is_running)
    return;

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
  const wifi_mac_header_t *mac_header = (const wifi_mac_header_t *)ppkt->payload;

  if (memcmp(mac_header->addr2, s_target_bssid, BSSID_LEN) == 0) {
    s_last_rssi = ppkt->rx_ctrl.rssi;
    s_last_seen_time = esp_timer_get_time();
  }
}

static void monitor_task(void *pvParameters) {
  while (s_is_running) {
    if (s_last_seen_time > 0) {
      int64_t now = esp_timer_get_time();
      int64_t diff_ms = (now - s_last_seen_time) / US_PER_MS;

      if (diff_ms > SIGNAL_TIMEOUT_MS) {
        s_last_rssi = RSSI_NO_SIGNAL;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(SIGNAL_CHECK_INTERVAL_MS));
  }
  vTaskDelete(NULL);
}
