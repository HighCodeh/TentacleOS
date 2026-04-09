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

#include "spi_bridge.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

#include "bt_dispatcher.h"
#include "cJSON.h"
#include "bluetooth_service.h"
#include "deauther_detector.h"
#include "signal_monitor.h"
#include "spi_slave_driver.h"
#include "storage_assets.h"
#include "wifi_dispatcher.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"

static const char *TAG = "SPI_BRIDGE_C5";

#define SPI_STREAM_QUEUE_LEN  8
#define SPI_BRIDGE_TASK_STACK 4096
#define SPI_BRIDGE_TASK_PRIO  10
#define SPI_IRQ_PULSE_MS      1
#define SPI_RESTART_DELAY_MS  50
#define SPI_WIFI_CMD_MIN      0x10
#define SPI_WIFI_CMD_MAX      0x4F
#define SPI_BT_CMD_MIN        0x50
#define SPI_BT_CMD_MAX        0x7F
#define SPI_FW_VERSION_LEN    32

typedef struct {
  spi_id_t id;
  uint8_t len;
  uint8_t data[SPI_MAX_PAYLOAD];
} spi_stream_item_t;

static void *s_data_source = NULL;
static uint16_t s_item_count = 0;
static const uint16_t *s_item_count_ptr = NULL;
static uint8_t s_item_size = 0;

static spi_stream_item_t s_stream_queue[SPI_STREAM_QUEUE_LEN];
static uint8_t s_stream_head = 0;
static uint8_t s_stream_tail = 0;
static uint8_t s_stream_count = 0;
static bool s_is_wifi_sniffer_streaming = false;
static bool s_is_bt_sniffer_streaming = false;
static portMUX_TYPE s_stream_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_is_restart_pending = false;
static char s_firmware_version[SPI_FW_VERSION_LEN] = "unknown";

static void load_firmware_version(void);
static bool stream_pop(spi_id_t *out_id, uint8_t *out_data, uint8_t *out_len);
static void bridge_task(void *pvParameters);

// Public functions

void spi_bridge_provide_results(void *source, uint16_t count, uint8_t item_size) {
  s_data_source = source;
  s_item_count = count;
  s_item_count_ptr = NULL;
  s_item_size = item_size;
}

void spi_bridge_provide_results_dynamic(void *source,
                                        const uint16_t *count_ptr,
                                        uint8_t item_size) {
  s_data_source = source;
  s_item_count = 0;
  s_item_count_ptr = count_ptr;
  s_item_size = item_size;
}

bool spi_bridge_stream_is_enabled(spi_id_t id) {
  if (id == SPI_ID_WIFI_APP_SNIFFER)
    return s_is_wifi_sniffer_streaming;
  if (id == SPI_ID_BT_APP_SNIFFER)
    return s_is_bt_sniffer_streaming;
  return false;
}

void spi_bridge_stream_enable(spi_id_t id, bool enable) {
  if (id == SPI_ID_WIFI_APP_SNIFFER)
    s_is_wifi_sniffer_streaming = enable;
  if (id == SPI_ID_BT_APP_SNIFFER)
    s_is_bt_sniffer_streaming = enable;
}

bool spi_bridge_stream_push(spi_id_t id, const uint8_t *data, uint8_t len) {
  if (!spi_bridge_stream_is_enabled(id))
    return false;
  if (data == NULL || len == 0)
    return false;
  if (len > SPI_MAX_PAYLOAD)
    return false;

  portENTER_CRITICAL(&s_stream_mux);
  if (s_stream_count >= SPI_STREAM_QUEUE_LEN) {
    portEXIT_CRITICAL(&s_stream_mux);
    return false;
  }
  spi_stream_item_t *item = &s_stream_queue[s_stream_tail];
  item->id = id;
  item->len = len;
  memcpy(item->data, data, len);
  s_stream_tail = (uint8_t)((s_stream_tail + 1) % SPI_STREAM_QUEUE_LEN);
  s_stream_count++;
  portEXIT_CRITICAL(&s_stream_mux);

  return true;
}

void spi_bridge_notify_master(void) {
  spi_slave_driver_set_irq(1);
  vTaskDelay(pdMS_TO_TICKS(SPI_IRQ_PULSE_MS));
  spi_slave_driver_set_irq(0);
}

esp_err_t spi_bridge_slave_init(void) {
  esp_err_t ret = spi_slave_driver_init();
  if (ret != ESP_OK)
    return ret;
  xTaskCreate(
      bridge_task, "spi_bridge_task", SPI_BRIDGE_TASK_STACK, NULL, SPI_BRIDGE_TASK_PRIO, NULL);
  return ESP_OK;
}

// Static functions

static void load_firmware_version(void) {
  size_t size;
  uint8_t *json_data = storage_assets_load_file("config/OTA/firmware.json", &size);
  if (json_data == NULL)
    return;

  cJSON *root = cJSON_ParseWithLength((const char *)json_data, size);
  free(json_data);
  if (root == NULL)
    return;

  cJSON *version = cJSON_GetObjectItem(root, "version");
  if (cJSON_IsString(version) && version->valuestring != NULL) {
    strncpy(s_firmware_version, version->valuestring, sizeof(s_firmware_version) - 1);
    s_firmware_version[sizeof(s_firmware_version) - 1] = '\0';
    ESP_LOGI(TAG, "Firmware version: %s", s_firmware_version);
  }
  cJSON_Delete(root);
}

static bool stream_pop(spi_id_t *out_id, uint8_t *out_data, uint8_t *out_len) {
  bool has_item = false;
  portENTER_CRITICAL(&s_stream_mux);
  if (s_stream_count > 0) {
    spi_stream_item_t *item = &s_stream_queue[s_stream_head];
    if (out_id != NULL)
      *out_id = item->id;
    if (out_len != NULL)
      *out_len = item->len;
    if (out_data != NULL && item->len > 0) {
      memcpy(out_data, item->data, item->len);
    }
    s_stream_head = (uint8_t)((s_stream_head + 1) % SPI_STREAM_QUEUE_LEN);
    s_stream_count--;
    has_item = true;
  }
  portEXIT_CRITICAL(&s_stream_mux);
  return has_item;
}

static void bridge_task(void *pvParameters) {
  uint8_t rx_buf[SPI_FRAME_SIZE];
  uint8_t tx_buf[SPI_FRAME_SIZE];

  while (1) {
    memset(rx_buf, 0, sizeof(rx_buf));
    if (spi_slave_driver_transmit(NULL, rx_buf, SPI_FRAME_SIZE) != ESP_OK)
      continue;

    spi_header_t *header = (spi_header_t *)rx_buf;
    if (header->sync != SPI_SYNC_BYTE || header->type != SPI_TYPE_CMD)
      continue;
    if (header->length > SPI_MAX_PAYLOAD)
      continue;

    spi_status_t status = SPI_STATUS_OK;
    uint8_t resp_payload[SPI_MAX_PAYLOAD];
    uint8_t resp_len = 0;

    if (header->id == SPI_ID_SYSTEM_PING) {
      status = SPI_STATUS_OK;
    } else if (header->id == SPI_ID_SYSTEM_REBOOT) {
      status = SPI_STATUS_OK;
      s_is_restart_pending = true;
    } else if (header->id == SPI_ID_SYSTEM_VERSION) {
      if (strcmp(s_firmware_version, "unknown") == 0)
        load_firmware_version();
      size_t ver_len = strlen(s_firmware_version);
      if (ver_len > (SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE))
        ver_len = (SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE);
      memcpy(resp_payload, s_firmware_version, ver_len);
      resp_len = (uint8_t)ver_len;
      status = SPI_STATUS_OK;
    } else if (header->id == SPI_ID_SYSTEM_STATUS) {
      spi_system_status_t sys = {.wifi_active = wifi_service_is_active() ? 1 : 0,
                                 .wifi_connected = wifi_service_is_connected() ? 1 : 0,
                                 .bt_running = bluetooth_service_is_running() ? 1 : 0,
                                 .bt_initialized = bluetooth_service_is_initialized() ? 1 : 0};
      memcpy(resp_payload, &sys, sizeof(sys));
      resp_len = sizeof(sys);
      status = SPI_STATUS_OK;
    } else if (header->id == SPI_ID_SYSTEM_DATA) {
      uint16_t index;
      memcpy(&index, rx_buf + sizeof(spi_header_t), sizeof(index));
      uint16_t item_count = s_item_count_ptr != NULL ? *s_item_count_ptr : s_item_count;

      if (index == SPI_DATA_INDEX_COUNT) {
        memcpy(resp_payload, &item_count, sizeof(item_count));
        resp_len = sizeof(item_count);
      } else if (index == SPI_DATA_INDEX_STATS) {
        spi_sniffer_stats_t stats = {.packets = wifi_sniffer_get_packet_count(),
                                     .deauths = wifi_sniffer_get_deauth_count(),
                                     .buffer_usage = wifi_sniffer_get_buffer_usage(),
                                     .signal_rssi = signal_monitor_get_rssi(),
                                     .handshake_captured = wifi_sniffer_handshake_captured(),
                                     .pmkid_captured = wifi_sniffer_pmkid_captured()};
        memcpy(resp_payload, &stats, sizeof(stats));
        resp_len = sizeof(stats);
      } else if (index == SPI_DATA_INDEX_DEAUTH_COUNT) {
        uint32_t deauth_count = deauther_detector_get_count();
        memcpy(resp_payload, &deauth_count, sizeof(deauth_count));
        resp_len = sizeof(deauth_count);
      } else if (s_data_source != NULL && index < item_count) {
        memcpy(resp_payload, (uint8_t *)s_data_source + (index * s_item_size), s_item_size);
        resp_len = s_item_size;
      } else {
        status = SPI_STATUS_ERROR;
      }
    } else if (header->id == SPI_ID_SYSTEM_STREAM) {
      spi_id_t stream_id = 0;
      uint8_t stream_len = 0;
      if (stream_pop(&stream_id, resp_payload, &stream_len)) {
        spi_header_t stream_header = {
            .sync = SPI_SYNC_BYTE, .type = SPI_TYPE_STREAM, .id = stream_id, .length = stream_len};
        memset(tx_buf, 0, sizeof(tx_buf));
        memcpy(tx_buf, &stream_header, sizeof(stream_header));
        if (stream_len > 0)
          memcpy(tx_buf + sizeof(stream_header), resp_payload, stream_len);

        spi_bridge_notify_master();
        spi_slave_driver_transmit(tx_buf, NULL, SPI_FRAME_SIZE);
        if (s_is_restart_pending) {
          vTaskDelay(pdMS_TO_TICKS(SPI_RESTART_DELAY_MS));
          esp_restart();
        }
        continue;
      }
      status = SPI_STATUS_BUSY;
    } else if (header->id >= SPI_WIFI_CMD_MIN && header->id <= SPI_WIFI_CMD_MAX) {
      status = wifi_dispatcher_execute(
          header->id, rx_buf + sizeof(spi_header_t), header->length, resp_payload, &resp_len);
    } else if (header->id >= SPI_BT_CMD_MIN && header->id <= SPI_BT_CMD_MAX) {
      status = bt_dispatcher_execute(
          header->id, rx_buf + sizeof(spi_header_t), header->length, resp_payload, &resp_len);
    } else {
      status = SPI_STATUS_UNSUPPORTED;
    }

    if (resp_len > (SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE)) {
      resp_len = 0;
      status = SPI_STATUS_ERROR;
    }

    spi_header_t resp_header = {.sync = SPI_SYNC_BYTE,
                                .type = SPI_TYPE_RESP,
                                .id = header->id,
                                .length = (uint8_t)(resp_len + SPI_RESP_STATUS_SIZE)};
    memset(tx_buf, 0, sizeof(tx_buf));
    memcpy(tx_buf, &resp_header, sizeof(resp_header));
    tx_buf[sizeof(resp_header)] = (uint8_t)status;
    if (resp_len > 0)
      memcpy(tx_buf + sizeof(resp_header) + SPI_RESP_STATUS_SIZE, resp_payload, resp_len);

    spi_bridge_notify_master();
    spi_slave_driver_transmit(tx_buf, NULL, SPI_FRAME_SIZE);

    if (s_is_restart_pending) {
      vTaskDelay(pdMS_TO_TICKS(SPI_RESTART_DELAY_MS));
      esp_restart();
    }
  }
}
