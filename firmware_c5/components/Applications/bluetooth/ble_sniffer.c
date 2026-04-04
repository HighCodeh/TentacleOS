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


#include "ble_sniffer.h"
#include "bluetooth_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "spi_bridge.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "BLE_SNIFFER";

#define SNIFFER_QUEUE_SIZE 20
#define SNIFFER_TASK_STACK_SIZE 4096

typedef struct {
  uint8_t addr[6];
  uint8_t addr_type;
  int rssi;
  uint8_t data[31];
  uint8_t len;
} sniffer_packet_t;

static QueueHandle_t sniffer_queue = NULL;
static TaskHandle_t sniffer_task_handle = NULL;
static StackType_t *sniffer_task_stack = NULL;
static StaticTask_t *sniffer_task_tcb = NULL;
static uint8_t *sniffer_queue_storage = NULL;
static StaticQueue_t *sniffer_queue_struct = NULL;

static void *sniffer_alloc(size_t size, const char *label) {
  void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (!ptr) {
    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr) {
      ESP_LOGW(TAG, "PSRAM alloc failed for %s, using internal RAM", label);
    }
  }
  return ptr;
}

static void sniffer_packet_handler(const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len) {
  if (sniffer_queue) {
    sniffer_packet_t packet;
    memcpy(packet.addr, addr, 6);
    packet.addr_type = addr_type;
    packet.rssi = rssi;
    packet.len = (len > 31) ? 31 : len;
    memcpy(packet.data, data, packet.len);

    xQueueSendFromISR(sniffer_queue, &packet, NULL);
  }
}

static void sniffer_task(void *pvParameters) {
  sniffer_packet_t packet;
  char mac_str[18];

  while (1) {
    if (xQueueReceive(sniffer_queue, &packet, portMAX_DELAY) == pdTRUE) {
      snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
               packet.addr[5], packet.addr[4], packet.addr[3], 
               packet.addr[2], packet.addr[1], packet.addr[0]);

      printf("[%s] %d dBm | ", mac_str, packet.rssi);
      for (int i = 0; i < packet.len; i++) {
        printf("%02X ", packet.data[i]);
      }
      printf("\n");

      if (spi_bridge_stream_is_enabled(SPI_ID_BT_APP_SNIFFER)) {
        spi_ble_sniffer_frame_t stream = {0};
        memcpy(stream.addr, packet.addr, 6);
        stream.addr_type = packet.addr_type;
        stream.rssi = (int8_t)packet.rssi;
        stream.len = packet.len;
        memcpy(stream.data, packet.data, packet.len);
        spi_bridge_stream_push(SPI_ID_BT_APP_SNIFFER, (const uint8_t *)&stream, sizeof(stream));
      }
    }
  }
}

esp_err_t ble_sniffer_start(void) {
  if (sniffer_task_handle != NULL) return ESP_OK;

  ESP_LOGI(TAG, "Starting BLE Packet Sniffer...");

  sniffer_task_stack = (StackType_t *)sniffer_alloc(SNIFFER_TASK_STACK_SIZE * sizeof(StackType_t), "task stack");
  sniffer_task_tcb = (StaticTask_t *)sniffer_alloc(sizeof(StaticTask_t), "task TCB");

  sniffer_queue_struct = (StaticQueue_t *)sniffer_alloc(sizeof(StaticQueue_t), "queue struct");
  sniffer_queue_storage = (uint8_t *)sniffer_alloc(SNIFFER_QUEUE_SIZE * sizeof(sniffer_packet_t), "queue storage");

  if (!sniffer_task_stack || !sniffer_task_tcb || !sniffer_queue_struct || !sniffer_queue_storage) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM memory for sniffer");
    if (sniffer_task_stack) heap_caps_free(sniffer_task_stack);
    if (sniffer_task_tcb) heap_caps_free(sniffer_task_tcb);
    if (sniffer_queue_struct) heap_caps_free(sniffer_queue_struct);
    if (sniffer_queue_storage) heap_caps_free(sniffer_queue_storage);
    return ESP_ERR_NO_MEM;
  }

  sniffer_queue = xQueueCreateStatic(SNIFFER_QUEUE_SIZE, sizeof(sniffer_packet_t), sniffer_queue_storage, sniffer_queue_struct);

  sniffer_task_handle = xTaskCreateStatic(
    sniffer_task, 
    "sniffer_task", 
    SNIFFER_TASK_STACK_SIZE, 
    NULL, 
    tskIDLE_PRIORITY + 1, 
    sniffer_task_stack, 
    sniffer_task_tcb
  );

  return bluetooth_service_start_sniffer(sniffer_packet_handler);
}

void ble_sniffer_stop(void) {
  bluetooth_service_stop_sniffer();

  if (sniffer_task_handle) {
    vTaskDelete(sniffer_task_handle);
    sniffer_task_handle = NULL;
  }

  if (sniffer_task_stack) { heap_caps_free(sniffer_task_stack); sniffer_task_stack = NULL; }
  if (sniffer_task_tcb) { heap_caps_free(sniffer_task_tcb); sniffer_task_tcb = NULL; }
  if (sniffer_queue_struct) { heap_caps_free(sniffer_queue_struct); sniffer_queue_struct = NULL; }
  if (sniffer_queue_storage) { heap_caps_free(sniffer_queue_storage); sniffer_queue_storage = NULL; }
  sniffer_queue = NULL;

  ESP_LOGI(TAG, "BLE Packet Sniffer Stopped.");
}

