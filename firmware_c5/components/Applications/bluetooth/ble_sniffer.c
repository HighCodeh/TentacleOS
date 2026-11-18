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
#include "freertos/idf_additions.h"
#include "sys_prio.h"

#include "bluetooth_service.h"
#include "session_manager.h"
#include "spi_bridge.h"

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

static QueueHandle_t s_sniffer_queue = NULL;
static TaskHandle_t s_sniffer_task_handle = NULL;
static uint8_t *s_sniffer_queue_storage = NULL;
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;
static StaticQueue_t *s_sniffer_queue_struct = NULL;
static portMUX_TYPE s_queue_lock = portMUX_INITIALIZER_UNLOCKED;

static void sniffer_task(void *pvParameters);

static void packet_handler(
    const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len) {
  if (addr == NULL || (data == NULL && len != 0))
    return;
  ble_sniffer_packet_t packet;
  memcpy(packet.addr, addr, 6);
  packet.addr_type = addr_type;
  packet.rssi = rssi;
  packet.len = (len > SNIFFER_MAX_DATA_LEN) ? SNIFFER_MAX_DATA_LEN : len;
  if (packet.len != 0)
    memcpy(packet.data, data, packet.len);

  // NimBLE delivers advertisements in task context. Guard late delivery at stop.
  portENTER_CRITICAL(&s_queue_lock);
  if (s_sniffer_queue != NULL)
    xQueueSend(s_sniffer_queue, &packet, 0);
  portEXIT_CRITICAL(&s_queue_lock);
}

esp_err_t ble_sniffer_start(void) {
  if (s_sniffer_task_handle != NULL)
    return ESP_OK;

  ESP_LOGI(TAG, "Starting BLE Packet Sniffer...");

  s_sniffer_queue_struct = (StaticQueue_t *)heap_caps_malloc(sizeof(StaticQueue_t),
                                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_sniffer_queue_storage = (uint8_t *)heap_caps_malloc(
      SNIFFER_QUEUE_SIZE * sizeof(ble_sniffer_packet_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_sniffer_queue_struct == NULL || s_sniffer_queue_storage == NULL)
    goto no_memory;

  s_sniffer_queue = xQueueCreateStatic(SNIFFER_QUEUE_SIZE,
                                       sizeof(ble_sniffer_packet_t),
                                       s_sniffer_queue_storage,
                                       s_sniffer_queue_struct);
  if (s_sniffer_queue == NULL)
    goto no_memory;

  // The SDK keeps the TCB internal while placing the bulk stack in PSRAM.
  if (xTaskCreatePinnedToCoreWithCaps(sniffer_task,
                                      "sniffer_task",
                                      SNIFFER_TASK_STACK_SIZE,
                                      NULL,
                                      SYS_PRIO_BACKGROUND_LO,
                                      &s_sniffer_task_handle,
                                      SYS_CORE_MAIN,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
    goto no_memory;

  esp_err_t err = bluetooth_service_start_sniffer(packet_handler);
  if (err != ESP_OK)
    ble_sniffer_stop();
  return err;

no_memory:
  ESP_LOGE(TAG, "Failed to allocate sniffer resources");
  ble_sniffer_stop();
  return ESP_ERR_NO_MEM;
}

void ble_sniffer_bind_session(uint32_t session_id) {
  s_session_id = session_id;
  spi_bridge_stream_enable(SPI_ID_BT_APP_SNIFFER, session_id != SPI_SESSION_INVALID_ID);
}

void ble_sniffer_session_killed(spi_id_t op_id) {
  (void)op_id;
  s_session_id = SPI_SESSION_INVALID_ID;
  ble_sniffer_stop();
}

void ble_sniffer_stop(void) {
  s_session_id = SPI_SESSION_INVALID_ID;
  spi_bridge_stream_enable(SPI_ID_BT_APP_SNIFFER, false);
  bluetooth_service_stop_sniffer();

  if (s_sniffer_task_handle != NULL) {
    vTaskDeleteWithCaps(s_sniffer_task_handle);
    s_sniffer_task_handle = NULL;
  }

  portENTER_CRITICAL(&s_queue_lock);
  QueueHandle_t queue = s_sniffer_queue;
  s_sniffer_queue = NULL;
  portEXIT_CRITICAL(&s_queue_lock);
  if (queue != NULL)
    vQueueDelete(queue);
  free(s_sniffer_queue_struct);
  s_sniffer_queue_struct = NULL;
  free(s_sniffer_queue_storage);
  s_sniffer_queue_storage = NULL;

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

      ESP_LOGD(TAG, "[%s] %d dBm | %d bytes", mac_str, packet.rssi, packet.len);

      if (spi_bridge_stream_is_enabled(SPI_ID_BT_APP_SNIFFER)) {
        spi_ble_sniffer_frame_t stream = {0};
        memcpy(stream.addr, packet.addr, 6);
        stream.addr_type = packet.addr_type;
        stream.rssi = (int8_t)packet.rssi;
        stream.len = packet.len;
        memcpy(stream.data, packet.data, packet.len);
        if (s_session_id != SPI_SESSION_INVALID_ID) {
          session_manager_try_emit(s_session_id, (const uint8_t *)&stream, sizeof(stream));
        } else {
          spi_bridge_stream_push(SPI_ID_BT_APP_SNIFFER, (const uint8_t *)&stream, sizeof(stream));
        }
      }
    }
  }
}
