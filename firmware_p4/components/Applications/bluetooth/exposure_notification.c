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

#include "exposure_notification.h"
#include "bluetooth_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "EXPOSURE_DETECTOR";

#define MAX_EXPOSURE_DEVICES 100
#define EXPOSURE_TTL_SEC 60
#define GAEN_UUID 0xFD6F

static exposure_device_t *device_list = NULL;
static uint16_t device_count = 0;
static SemaphoreHandle_t list_mutex = NULL;
static bool is_running = false;

#define BLE_AD_TYPE_16BIT_SERV_UUID_MORE  0x02
#define BLE_AD_TYPE_16BIT_SERV_UUID_CMPL  0x03
#define BLE_AD_TYPE_SERVICE_DATA          0x16

static void cleanup_list() {
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000); // Seconds
  for (int i = 0; i < device_count; ) {
    if (now - device_list[i].last_seen > EXPOSURE_TTL_SEC) {
      if (i < device_count - 1) {
        memmove(&device_list[i], &device_list[i+1], sizeof(exposure_device_t) * (device_count - 1 - i));
      }
      device_count--;
    } else {
      i++;
    }
  }
}

static bool parse_for_gaen(const uint8_t *data, uint16_t len) {
  uint16_t pos = 0;
  while (pos < len) {
    uint8_t field_len = data[pos];
    if (field_len == 0) break; // Padding
    if (pos + 1 + field_len > len) break; // Malformed

    uint8_t field_type = data[pos + 1];
    const uint8_t *field_data = &data[pos + 2];
    uint8_t field_data_len = field_len - 1;

    if (field_type == BLE_AD_TYPE_SERVICE_DATA) {
      if (field_data_len >= 2) {
        uint16_t uuid = field_data[0] | (field_data[1] << 8);
        if (uuid == GAEN_UUID) {
          return true;
        }
      }
    }
    else if (field_type == BLE_AD_TYPE_16BIT_SERV_UUID_MORE || 
      field_type == BLE_AD_TYPE_16BIT_SERV_UUID_CMPL) {
      for (int i = 0; i < field_data_len; i += 2) {
        if (i + 1 < field_data_len) {
          uint16_t uuid = field_data[i] | (field_data[i+1] << 8);
          if (uuid == GAEN_UUID) {
            return true;
          }
        }
      }
    }

    pos += (field_len + 1);
  }
  return false;
}

static void sniffer_callback(const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len) {
  if (parse_for_gaen(data, len)) {
    if (xSemaphoreTake(list_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      bool found = false;
      uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);

      for (int i = 0; i < device_count; i++) {
        if (memcmp(device_list[i].addr, addr, 6) == 0) {
          device_list[i].rssi = rssi;
          device_list[i].last_seen = now;
          found = true;
          break;
        }
      }

      if (!found && device_count < MAX_EXPOSURE_DEVICES) {
        memcpy(device_list[device_count].addr, addr, 6);
        device_list[device_count].rssi = rssi;
        device_list[device_count].last_seen = now;
        device_count++;
        ESP_LOGI(TAG, "GAEN Device Found! Count: %d", device_count);
      }

      if (device_count == MAX_EXPOSURE_DEVICES) {
        cleanup_list();
      }

      xSemaphoreGive(list_mutex);
    }
  }
}

esp_err_t exposure_notification_start(void) {
  if (is_running) return ESP_OK;

  if (list_mutex == NULL) {
    list_mutex = xSemaphoreCreateMutex();
  }

  if (device_list == NULL) {
    device_list = (exposure_device_t *)heap_caps_malloc(MAX_EXPOSURE_DEVICES * sizeof(exposure_device_t), MALLOC_CAP_SPIRAM);
    if (!device_list) {
      ESP_LOGE(TAG, "Failed to allocate memory");
      return ESP_ERR_NO_MEM;
    }
  }

  device_count = 0;
  is_running = true;

  return bluetooth_service_start_sniffer(sniffer_callback);
}

void exposure_notification_stop(void) {
  if (!is_running) return;
  bluetooth_service_stop_sniffer();
  is_running = false;
}

exposure_device_t* exposure_notification_get_list(uint16_t *count) {
  if (xSemaphoreTake(list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    cleanup_list();
    *count = device_count;
    xSemaphoreGive(list_mutex);
    return device_list;
  }
  *count = 0;
  return NULL;
}

uint16_t exposure_notification_get_count(void) {
  uint16_t count = 0;
  if (xSemaphoreTake(list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    cleanup_list();
    count = device_count;
    xSemaphoreGive(list_mutex);
  }
  return count;
}

void exposure_notification_reset(void) {
  if (xSemaphoreTake(list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    device_count = 0;
    xSemaphoreGive(list_mutex);
  }
}
