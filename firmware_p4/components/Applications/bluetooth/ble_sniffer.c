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

#include "ble_sniffer.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "bluetooth_service.h"

static const char *TAG = "BLE_SNIFFER";

#define SNIFFER_QUEUE_SIZE      20
#define SNIFFER_TASK_STACK_SIZE 4096
#define SNIFFER_MAX_DATA_LEN    31

typedef struct {
  uint8_t addr[6];
  uint8_t addr_type;
  int rssi;
  uint8_t data[SNIFFER_MAX_DATA_LEN];
  uint8_t len;
} ble_sniffer_packet_t;

static ble_sniffer_observer_t s_observer = NULL;

static QueueHandle_t s_sniffer_queue = NULL;
static TaskHandle_t s_sniffer_task_handle = NULL;
static StackType_t *s_sniffer_task_stack = NULL;
static StaticTask_t *s_sniffer_task_tcb = NULL;
static uint8_t *s_sniffer_queue_storage = NULL;
static StaticQueue_t *s_sniffer_queue_struct = NULL;

static void sniffer_task(void *pvParameters);

void ble_sniffer_set_observer(ble_sniffer_observer_t cb) {
  s_observer = cb;
}

static void packet_handler(
    const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len) {
  if (s_sniffer_queue != NULL) {
    ble_sniffer_packet_t packet;
    memcpy(packet.addr, addr, 6);
    packet.addr_type = addr_type;
    packet.rssi = rssi;
    packet.len = (len > SNIFFER_MAX_DATA_LEN) ? SNIFFER_MAX_DATA_LEN : len;
    memcpy(packet.data, data, packet.len);

    xQueueSendFromISR(s_sniffer_queue, &packet, NULL);
  }
}

esp_err_t ble_sniffer_start(void) {
  if (s_sniffer_task_handle != NULL)
    return ESP_OK;

  ESP_LOGI(TAG, "Starting BLE Packet Sniffer...");

  s_sniffer_task_stack = (StackType_t *)heap_caps_malloc(
      SNIFFER_TASK_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_sniffer_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);

  s_sniffer_queue_struct =
      (StaticQueue_t *)heap_caps_malloc(sizeof(StaticQueue_t), MALLOC_CAP_SPIRAM);
  s_sniffer_queue_storage = (uint8_t *)heap_caps_malloc(
      SNIFFER_QUEUE_SIZE * sizeof(ble_sniffer_packet_t), MALLOC_CAP_SPIRAM);

  if (s_sniffer_task_stack == NULL || s_sniffer_task_tcb == NULL ||
      s_sniffer_queue_struct == NULL || s_sniffer_queue_storage == NULL) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM memory for sniffer");
    free(s_sniffer_task_stack);
    s_sniffer_task_stack = NULL;
    free(s_sniffer_task_tcb);
    s_sniffer_task_tcb = NULL;
    free(s_sniffer_queue_struct);
    s_sniffer_queue_struct = NULL;
    free(s_sniffer_queue_storage);
    s_sniffer_queue_storage = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_sniffer_queue = xQueueCreateStatic(SNIFFER_QUEUE_SIZE,
                                       sizeof(ble_sniffer_packet_t),
                                       s_sniffer_queue_storage,
                                       s_sniffer_queue_struct);

  s_sniffer_task_handle = xTaskCreateStaticPinnedToCore(sniffer_task,
                                                        "sniffer_task",
                                                        SNIFFER_TASK_STACK_SIZE,
                                                        NULL,
                                                        SYS_PRIO_BACKGROUND_LO,
                                                        s_sniffer_task_stack,
                                                        s_sniffer_task_tcb,
                                                        SYS_CORE_RADIO);

  return bluetooth_service_start_sniffer(packet_handler);
}

void ble_sniffer_stop(void) {
  s_observer = NULL;
  bluetooth_service_stop_sniffer();

  if (s_sniffer_task_handle != NULL) {
    vTaskDelete(s_sniffer_task_handle);
    s_sniffer_task_handle = NULL;
  }

  if (s_sniffer_task_stack != NULL) {
    free(s_sniffer_task_stack);
    s_sniffer_task_stack = NULL;
  }
  if (s_sniffer_task_tcb != NULL) {
    free(s_sniffer_task_tcb);
    s_sniffer_task_tcb = NULL;
  }
  if (s_sniffer_queue_struct != NULL) {
    free(s_sniffer_queue_struct);
    s_sniffer_queue_struct = NULL;
  }
  if (s_sniffer_queue_storage != NULL) {
    free(s_sniffer_queue_storage);
    s_sniffer_queue_storage = NULL;
  }
  s_sniffer_queue = NULL;

  ESP_LOGI(TAG, "BLE Packet Sniffer Stopped.");
}

static void sniffer_task(void *pvParameters) {
  ble_sniffer_packet_t packet;
  char mac_str[18];

  while (1) {
    if (xQueueReceive(s_sniffer_queue, &packet, portMAX_DELAY) == pdTRUE) {
      snprintf(mac_str,
               sizeof(mac_str),
               "%02X:%02X:%02X:%02X:%02X:%02X",
               packet.addr[5],
               packet.addr[4],
               packet.addr[3],
               packet.addr[2],
               packet.addr[1],
               packet.addr[0]);

      printf("[%s] %d dBm | ", mac_str, packet.rssi);
      for (int i = 0; i < packet.len; i++) {
        printf("%02X ", packet.data[i]);
      }
      printf("\n");

      ble_sniffer_observer_t obs = s_observer;
      if (obs != NULL) {
        ble_sniffer_adv_t adv = {
            .addr = packet.addr,
            .addr_type = packet.addr_type,
            .rssi = packet.rssi,
            .data = packet.data,
            .len = packet.len,
        };
        obs(&adv);
      }
    }
  }
}
