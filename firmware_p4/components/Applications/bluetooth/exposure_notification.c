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

#include "exposure_notification.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "bluetooth_service.h"

static const char *TAG = "EXPOSURE_NOTIFICATION";

#define MAX_EXPOSURE_DEVICES  100
#define EXPOSURE_TTL_SEC      60
#define GAEN_UUID             0xFD6F
#define MUTEX_TIMEOUT_MS      50
#define MUTEX_TIMEOUT_LONG_MS 100
#define USEC_PER_SEC          1000000

#define BLE_AD_TYPE_16BIT_SERV_UUID_MORE 0x02
#define BLE_AD_TYPE_16BIT_SERV_UUID_CMPL 0x03
#define BLE_AD_TYPE_SERVICE_DATA         0x16

static exposure_notification_device_t *s_device_list = NULL;
static uint16_t s_device_count = 0;
static SemaphoreHandle_t s_list_mutex = NULL;
static volatile bool s_is_running = false;

static void cleanup_list(void);
static bool parse_for_gaen(const uint8_t *data, uint16_t len);
static void sniffer_callback(
    const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len);

esp_err_t exposure_notification_start(void) {
  if (s_is_running)
    return ESP_OK;

  if (s_list_mutex == NULL) {
    s_list_mutex = xSemaphoreCreateMutex();
    if (s_list_mutex == NULL)
      return ESP_ERR_NO_MEM;
  }

  if (s_device_list == NULL) {
    s_device_list = (exposure_notification_device_t *)heap_caps_malloc(
        MAX_EXPOSURE_DEVICES * sizeof(exposure_notification_device_t), MALLOC_CAP_SPIRAM);
    if (s_device_list == NULL) {
      ESP_LOGE(TAG, "Failed to allocate memory");
      return ESP_ERR_NO_MEM;
    }
  }

  s_device_count = 0;
  s_is_running = true;

  esp_err_t err = bluetooth_service_start_sniffer(sniffer_callback);
  if (err != ESP_OK)
    s_is_running = false;
  return err;
}

void exposure_notification_stop(void) {
  if (!s_is_running)
    return;
  bluetooth_service_stop_sniffer();
  s_is_running = false;
}

exposure_notification_device_t *exposure_notification_get_list(uint16_t *out_count) {
  if (out_count == NULL)
    return NULL;
  if (s_list_mutex != NULL &&
      xSemaphoreTake(s_list_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_LONG_MS)) == pdTRUE) {
    cleanup_list();
    *out_count = s_device_count;
    xSemaphoreGive(s_list_mutex);
    return s_device_list;
  }
  *out_count = 0;
  return NULL;
}

uint16_t exposure_notification_get_count(void) {
  uint16_t count = 0;
  if (s_list_mutex != NULL &&
      xSemaphoreTake(s_list_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_LONG_MS)) == pdTRUE) {
    cleanup_list();
    count = s_device_count;
    xSemaphoreGive(s_list_mutex);
  }
  return count;
}

void exposure_notification_reset(void) {
  if (s_list_mutex != NULL &&
      xSemaphoreTake(s_list_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_LONG_MS)) == pdTRUE) {
    s_device_count = 0;
    xSemaphoreGive(s_list_mutex);
  }
}

static void cleanup_list(void) {
  uint32_t now = (uint32_t)(esp_timer_get_time() / USEC_PER_SEC);
  for (int i = 0; i < s_device_count;) {
    if (now - s_device_list[i].last_seen > EXPOSURE_TTL_SEC) {
      if (i < s_device_count - 1) {
        memmove(&s_device_list[i],
                &s_device_list[i + 1],
                sizeof(exposure_notification_device_t) * (s_device_count - 1 - i));
      }
      s_device_count--;
    } else {
      i++;
    }
  }
}

static bool parse_for_gaen(const uint8_t *data, uint16_t len) {
  uint16_t pos = 0;
  while (pos < len) {
    uint8_t field_len = data[pos];
    if (field_len == 0)
      break;
    if (pos + 1 + field_len > len)
      break;

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
    } else if (field_type == BLE_AD_TYPE_16BIT_SERV_UUID_MORE ||
               field_type == BLE_AD_TYPE_16BIT_SERV_UUID_CMPL) {
      for (int i = 0; i < field_data_len; i += 2) {
        if (i + 1 < field_data_len) {
          uint16_t uuid = field_data[i] | (field_data[i + 1] << 8);
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

static void sniffer_callback(
    const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len) {
  if (parse_for_gaen(data, len)) {
    if (s_list_mutex != NULL &&
        xSemaphoreTake(s_list_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
      bool is_found = false;
      uint32_t now = (uint32_t)(esp_timer_get_time() / USEC_PER_SEC);

      for (int i = 0; i < s_device_count; i++) {
        if (memcmp(s_device_list[i].addr, addr, 6) == 0) {
          s_device_list[i].rssi = rssi;
          s_device_list[i].last_seen = now;
          is_found = true;
          break;
        }
      }

      if (!is_found && s_device_count < MAX_EXPOSURE_DEVICES) {
        memcpy(s_device_list[s_device_count].addr, addr, 6);
        s_device_list[s_device_count].rssi = rssi;
        s_device_list[s_device_count].last_seen = now;
        s_device_count++;
        ESP_LOGI(TAG, "GAEN Device Found! Count: %d", s_device_count);
      }

      if (s_device_count == MAX_EXPOSURE_DEVICES) {
        cleanup_list();
      }

      xSemaphoreGive(s_list_mutex);
    }
  }
}
