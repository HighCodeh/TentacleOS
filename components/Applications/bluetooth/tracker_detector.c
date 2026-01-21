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

#include "tracker_detector.h"
#include "bluetooth_service.h"
#include "host/ble_hs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "TRACKER_DETECTOR";

#define TRACKER_TASK_STACK_SIZE 4096
#define TRACKER_TIMEOUT_MS 30000

#define MFG_ID_APPLE   0x004C
#define MFG_ID_SAMSUNG 0x0075
#define MFG_ID_TILE    0x0059

static tracker_record_t *found_trackers = NULL;
static uint16_t tracker_count = 0;
static bool is_running = false;

static TaskHandle_t tracker_task_handle = NULL;
static StackType_t *tracker_task_stack = NULL;
static StaticTask_t *tracker_task_tcb = NULL;
static SemaphoreHandle_t list_mutex = NULL;

static tracker_type_t identify_tracker(const uint8_t *mfg_data, uint8_t len) {
  if (len < 2) return TRACKER_TYPE_UNKNOWN;
  uint16_t mfg_id = (uint16_t)mfg_data[0] | ((uint16_t)mfg_data[1] << 8);

  if (mfg_id == MFG_ID_APPLE) {
    if (len > 2) {
      uint8_t type = mfg_data[2];
      if (type == 0x12 || type == 0x10 || type == 0x07 || type == 0x05) return TRACKER_TYPE_AIRTAG;
    }
    return TRACKER_TYPE_AIRTAG; 
  }
  if (mfg_id == MFG_ID_SAMSUNG) return TRACKER_TYPE_SMARTTAG;
  if (mfg_id == MFG_ID_TILE) return TRACKER_TYPE_TILE;

  return TRACKER_TYPE_UNKNOWN;
}

static const char* get_type_str(tracker_type_t type) {
  switch(type) {
    case TRACKER_TYPE_AIRTAG: return "AirTag (Apple)";
    case TRACKER_TYPE_SMARTTAG: return "SmartTag (Samsung)";
    case TRACKER_TYPE_TILE: return "Tile";
    default: return "Unknown";
  }
}

static void scanner_callback(const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len) {
  struct ble_hs_adv_fields fields;
  if (ble_hs_adv_parse_fields(&fields, data, len) != 0) return;

  if (fields.mfg_data != NULL && fields.mfg_data_len >= 2) {
    tracker_type_t type = identify_tracker(fields.mfg_data, fields.mfg_data_len);

    if (type != TRACKER_TYPE_UNKNOWN) {
      if (xSemaphoreTake(list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool found = false;
        for (int i = 0; i < tracker_count; i++) {
          if (memcmp(found_trackers[i].addr.val, addr, 6) == 0) {
            found_trackers[i].rssi = rssi;
            found_trackers[i].last_seen = (uint32_t)(esp_timer_get_time() / 1000);
            if (found_trackers[i].type == TRACKER_TYPE_UNKNOWN && type != TRACKER_TYPE_UNKNOWN) {
              found_trackers[i].type = type;
              strncpy(found_trackers[i].type_str, get_type_str(type), 15);
            }
            found = true;
            break;
          }
        }

        if (!found && tracker_count < MAX_TRACKERS_FOUND) {
          tracker_record_t *rec = &found_trackers[tracker_count];
          memcpy(rec->addr.val, addr, 6);
          rec->addr.type = addr_type;
          rec->rssi = rssi;
          rec->type = type;
          strncpy(rec->type_str, get_type_str(type), 15);
          rec->type_str[15] = '\0';
          rec->last_seen = (uint32_t)(esp_timer_get_time() / 1000);
          int copy_len = (fields.mfg_data_len > 10) ? 10 : fields.mfg_data_len;
          memcpy(rec->payload_sample, fields.mfg_data, copy_len);
          tracker_count++;
          ESP_LOGI(TAG, "Tracker Found: %s RSSI: %d", rec->type_str, rec->rssi);
        }
        xSemaphoreGive(list_mutex);
      }
    }
  }
}

static void tracker_monitor_task(void *pvParameters) {
  bluetooth_service_start_sniffer(scanner_callback);
  while (is_running) {
    if (xSemaphoreTake(list_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
      for (int i = 0; i < tracker_count; ) {
        if ((now - found_trackers[i].last_seen) > TRACKER_TIMEOUT_MS) {
          if (i < tracker_count - 1) {
            memmove(&found_trackers[i], &found_trackers[i+1], sizeof(tracker_record_t) * (tracker_count - 1 - i));
          }
          tracker_count--;
        } else {
          i++;
        }
      }
      xSemaphoreGive(list_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  bluetooth_service_stop_sniffer();
  tracker_task_handle = NULL;
  if (tracker_task_stack) { heap_caps_free(tracker_task_stack); tracker_task_stack = NULL; }
  if (tracker_task_tcb) { heap_caps_free(tracker_task_tcb); tracker_task_tcb = NULL; }
  vTaskDelete(NULL);
}

bool tracker_detector_start(void) {
  if (is_running) return false;
  if (list_mutex == NULL) list_mutex = xSemaphoreCreateMutex();
  if (found_trackers == NULL) {
    found_trackers = (tracker_record_t *)heap_caps_malloc(MAX_TRACKERS_FOUND * sizeof(tracker_record_t), MALLOC_CAP_SPIRAM);
    if (!found_trackers) return false;
  }
  is_running = true;
  tracker_task_stack = (StackType_t *)heap_caps_malloc(TRACKER_TASK_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  tracker_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
  if (!tracker_task_stack || !tracker_task_tcb) {
    if (tracker_task_stack) heap_caps_free(tracker_task_stack);
    if (tracker_task_tcb) heap_caps_free(tracker_task_tcb);
    is_running = false;
    return false;
  }
  tracker_task_handle = xTaskCreateStatic(tracker_monitor_task, "tracker_task", TRACKER_TASK_STACK_SIZE, NULL, 5, tracker_task_stack, tracker_task_tcb);
  return (tracker_task_handle != NULL);
}

void tracker_detector_stop(void) {
  is_running = false;
}

tracker_record_t* tracker_detector_get_results(uint16_t *count) {
  *count = tracker_count;
  return found_trackers;
}

void tracker_detector_clear_results(void) {
  if (xSemaphoreTake(list_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    tracker_count = 0;
    xSemaphoreGive(list_mutex);
  }
}
