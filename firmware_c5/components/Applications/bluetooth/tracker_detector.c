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

#include "tracker_detector.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "host/ble_hs.h"

#include "bluetooth_service.h"

static const char *TAG = "TRACKER_DETECTOR";

#define TRACKER_TASK_STACK_SIZE  4096
#define TRACKER_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define TRACKER_TIMEOUT_MS       30000
#define TRACKER_POLL_DELAY_MS    1000
#define TRACKER_MUTEX_TIMEOUT_MS 100
#define TRACKER_MUTEX_POLL_MS    500
#define TRACKER_MUTEX_CLEAR_MS   200
#define TRACKER_TYPE_STR_MAX_LEN 16
#define USEC_PER_MSEC            1000

#define MFG_ID_APPLE   0x004C
#define MFG_ID_SAMSUNG 0x0075
#define MFG_ID_TILE    0x0059

static tracker_detector_record_t *s_found_trackers = NULL;
static uint16_t s_tracker_count = 0;
static volatile bool s_is_running = false;

static TaskHandle_t s_tracker_task_handle = NULL;
static StackType_t *s_tracker_task_stack = NULL;
static StaticTask_t *s_tracker_task_tcb = NULL;
static SemaphoreHandle_t s_list_mutex = NULL;

static tracker_detector_type_t identify_tracker(const uint8_t *mfg_data, uint8_t len);
static const char *get_type_str(tracker_detector_type_t type);
static void scanner_callback(
    const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len);
static void monitor_task(void *pvParameters);

bool tracker_detector_start(void) {
  if (s_is_running)
    return false;
  if (s_list_mutex == NULL)
    s_list_mutex = xSemaphoreCreateMutex();
  if (s_found_trackers == NULL) {
    s_found_trackers = (tracker_detector_record_t *)heap_caps_malloc(
        TRACKER_DETECTOR_MAX_RESULTS * sizeof(tracker_detector_record_t), MALLOC_CAP_SPIRAM);
    if (s_found_trackers == NULL)
      return false;
  }
  s_is_running = true;
  s_tracker_task_stack = (StackType_t *)heap_caps_malloc(
      TRACKER_TASK_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_tracker_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
  if (s_tracker_task_stack == NULL || s_tracker_task_tcb == NULL) {
    free(s_tracker_task_stack);
    s_tracker_task_stack = NULL;
    free(s_tracker_task_tcb);
    s_tracker_task_tcb = NULL;
    s_is_running = false;
    return false;
  }
  s_tracker_task_handle = xTaskCreateStatic(monitor_task,
                                            "tracker_task",
                                            TRACKER_TASK_STACK_SIZE,
                                            NULL,
                                            TRACKER_TASK_PRIORITY,
                                            s_tracker_task_stack,
                                            s_tracker_task_tcb);
  return (s_tracker_task_handle != NULL);
}

void tracker_detector_stop(void) {
  s_is_running = false;
}

tracker_detector_record_t *tracker_detector_get_results(uint16_t *out_count) {
  *out_count = s_tracker_count;
  return s_found_trackers;
}

void tracker_detector_clear_results(void) {
  if (xSemaphoreTake(s_list_mutex, pdMS_TO_TICKS(TRACKER_MUTEX_CLEAR_MS)) == pdTRUE) {
    s_tracker_count = 0;
    xSemaphoreGive(s_list_mutex);
  }
}

static tracker_detector_type_t identify_tracker(const uint8_t *mfg_data, uint8_t len) {
  if (len < 2)
    return TRACKER_DETECTOR_TYPE_UNKNOWN;
  uint16_t mfg_id = (uint16_t)mfg_data[0] | ((uint16_t)mfg_data[1] << 8);

  if (mfg_id == MFG_ID_APPLE) {
    if (len > 2) {
      uint8_t type = mfg_data[2];
      if (type == 0x12 || type == 0x10 || type == 0x07 || type == 0x05)
        return TRACKER_DETECTOR_TYPE_AIRTAG;
    }
    return TRACKER_DETECTOR_TYPE_AIRTAG;
  }
  if (mfg_id == MFG_ID_SAMSUNG)
    return TRACKER_DETECTOR_TYPE_SMARTTAG;
  if (mfg_id == MFG_ID_TILE)
    return TRACKER_DETECTOR_TYPE_TILE;

  return TRACKER_DETECTOR_TYPE_UNKNOWN;
}

static const char *get_type_str(tracker_detector_type_t type) {
  switch (type) {
    case TRACKER_DETECTOR_TYPE_AIRTAG:
      return "AirTag (Apple)";
    case TRACKER_DETECTOR_TYPE_SMARTTAG:
      return "SmartTag (Samsung)";
    case TRACKER_DETECTOR_TYPE_TILE:
      return "Tile";
    default:
      return "Unknown";
  }
}

static void scanner_callback(
    const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len) {
  struct ble_hs_adv_fields fields;
  if (ble_hs_adv_parse_fields(&fields, data, len) != 0)
    return;

  if (fields.mfg_data != NULL && fields.mfg_data_len >= 2) {
    tracker_detector_type_t type = identify_tracker(fields.mfg_data, fields.mfg_data_len);

    if (type != TRACKER_DETECTOR_TYPE_UNKNOWN) {
      if (xSemaphoreTake(s_list_mutex, pdMS_TO_TICKS(TRACKER_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        bool is_found = false;
        for (int i = 0; i < s_tracker_count; i++) {
          if (memcmp(s_found_trackers[i].addr.val, addr, 6) == 0) {
            s_found_trackers[i].rssi = rssi;
            s_found_trackers[i].last_seen = (uint32_t)(esp_timer_get_time() / USEC_PER_MSEC);
            if (s_found_trackers[i].type == TRACKER_DETECTOR_TYPE_UNKNOWN &&
                type != TRACKER_DETECTOR_TYPE_UNKNOWN) {
              s_found_trackers[i].type = type;
              strncpy(
                  s_found_trackers[i].type_str, get_type_str(type), TRACKER_TYPE_STR_MAX_LEN - 1);
            }
            is_found = true;
            break;
          }
        }

        if (!is_found && s_tracker_count < TRACKER_DETECTOR_MAX_RESULTS) {
          tracker_detector_record_t *rec = &s_found_trackers[s_tracker_count];
          memcpy(rec->addr.val, addr, 6);
          rec->addr.type = addr_type;
          rec->rssi = rssi;
          rec->type = type;
          strncpy(rec->type_str, get_type_str(type), TRACKER_TYPE_STR_MAX_LEN - 1);
          rec->type_str[TRACKER_TYPE_STR_MAX_LEN - 1] = '\0';
          rec->last_seen = (uint32_t)(esp_timer_get_time() / USEC_PER_MSEC);
          int copy_len = (fields.mfg_data_len > TRACKER_DETECTOR_PAYLOAD_SAMPLE_SIZE)
                             ? TRACKER_DETECTOR_PAYLOAD_SAMPLE_SIZE
                             : fields.mfg_data_len;
          memcpy(rec->payload_sample, fields.mfg_data, copy_len);
          s_tracker_count++;
          ESP_LOGI(TAG, "Tracker Found: %s RSSI: %d", rec->type_str, rec->rssi);
        }
        xSemaphoreGive(s_list_mutex);
      }
    }
  }
}

static void monitor_task(void *pvParameters) {
  bluetooth_service_start_sniffer(scanner_callback);
  while (s_is_running) {
    if (xSemaphoreTake(s_list_mutex, pdMS_TO_TICKS(TRACKER_MUTEX_POLL_MS)) == pdTRUE) {
      uint32_t now = (uint32_t)(esp_timer_get_time() / USEC_PER_MSEC);
      for (int i = 0; i < s_tracker_count;) {
        if ((now - s_found_trackers[i].last_seen) > TRACKER_TIMEOUT_MS) {
          if (i < s_tracker_count - 1) {
            memmove(&s_found_trackers[i],
                    &s_found_trackers[i + 1],
                    sizeof(tracker_detector_record_t) * (s_tracker_count - 1 - i));
          }
          s_tracker_count--;
        } else {
          i++;
        }
      }
      xSemaphoreGive(s_list_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(TRACKER_POLL_DELAY_MS));
  }
  bluetooth_service_stop_sniffer();
  s_tracker_task_handle = NULL;
  if (s_tracker_task_stack != NULL) {
    free(s_tracker_task_stack);
    s_tracker_task_stack = NULL;
  }
  if (s_tracker_task_tcb != NULL) {
    free(s_tracker_task_tcb);
    s_tracker_task_tcb = NULL;
  }
  vTaskDelete(NULL);
}
