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

#include "skimmer_detector.h"

#include <ctype.h>
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

static const char *TAG = "SKIMMER_DETECTOR";

#define SKIMMER_TASK_STACK_SIZE  4096
#define SKIMMER_TASK_PRIORITY    SYS_PRIO_SERVICE_HI
#define SKIMMER_TIMEOUT_MS       30000
#define SKIMMER_POLL_DELAY_MS    1000
#define SKIMMER_MUTEX_TIMEOUT_MS 100
#define SKIMMER_MUTEX_POLL_MS    500
#define SKIMMER_MUTEX_CLEAR_MS   200
#define SKIMMER_NAME_MAX_LEN     32
#define SKIMMER_REASON_MAX_LEN   32
#define USEC_PER_MSEC            1000

static skimmer_detector_record_t *s_found_skimmers = NULL;
static uint16_t s_skimmer_count = 0;
static volatile bool s_is_running = false;

static TaskHandle_t s_skimmer_task_handle = NULL;
static StackType_t *s_skimmer_task_stack = NULL;
static StaticTask_t *s_skimmer_task_tcb = NULL;
static SemaphoreHandle_t s_list_mutex = NULL;

static const char *SUSPICIOUS_NAMES[] = {"HC-05",
                                         "HC-06",
                                         "HC-08",
                                         "HM-10",
                                         "HM-Soft",
                                         "CC41",
                                         "MLT-BT05",
                                         "JDY-",
                                         "AT-09",
                                         "BT05",
                                         "SPP-CA"};
#define SUSPICIOUS_NAMES_COUNT (sizeof(SUSPICIOUS_NAMES) / sizeof(SUSPICIOUS_NAMES[0]))

static void check_suspicious(const char *device_name, char *reason_out);
static void scanner_callback(
    const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len);
static void monitor_task(void *pvParameters);

bool skimmer_detector_start(void) {
  if (s_is_running)
    return false;
  if (s_list_mutex == NULL)
    s_list_mutex = xSemaphoreCreateMutex();
  if (s_found_skimmers == NULL) {
    s_found_skimmers = (skimmer_detector_record_t *)heap_caps_malloc(
        SKIMMER_DETECTOR_MAX_RESULTS * sizeof(skimmer_detector_record_t), MALLOC_CAP_SPIRAM);
    if (s_found_skimmers == NULL)
      return false;
  }
  s_is_running = true;
  s_skimmer_task_stack = (StackType_t *)heap_caps_malloc(
      SKIMMER_TASK_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  s_skimmer_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
  if (s_skimmer_task_stack == NULL || s_skimmer_task_tcb == NULL) {
    free(s_skimmer_task_stack);
    s_skimmer_task_stack = NULL;
    free(s_skimmer_task_tcb);
    s_skimmer_task_tcb = NULL;
    s_is_running = false;
    return false;
  }
  s_skimmer_task_handle = xTaskCreateStatic(monitor_task,
                                            "skimmer_task",
                                            SKIMMER_TASK_STACK_SIZE,
                                            NULL,
                                            SKIMMER_TASK_PRIORITY,
                                            s_skimmer_task_stack,
                                            s_skimmer_task_tcb);
  return (s_skimmer_task_handle != NULL);
}

void skimmer_detector_stop(void) {
  s_is_running = false;
}

skimmer_detector_record_t *skimmer_detector_get_results(uint16_t *out_count) {
  *out_count = s_skimmer_count;
  return s_found_skimmers;
}

void skimmer_detector_clear_results(void) {
  if (xSemaphoreTake(s_list_mutex, pdMS_TO_TICKS(SKIMMER_MUTEX_CLEAR_MS)) == pdTRUE) {
    s_skimmer_count = 0;
    xSemaphoreGive(s_list_mutex);
  }
}

static void check_suspicious(const char *device_name, char *reason_out) {
  if (strlen(device_name) == 0)
    return;

  char upper_name[SKIMMER_NAME_MAX_LEN + 1];
  char upper_suspicious[SKIMMER_NAME_MAX_LEN + 1];

  strncpy(upper_name, device_name, SKIMMER_NAME_MAX_LEN);
  upper_name[SKIMMER_NAME_MAX_LEN] = '\0';
  for (int k = 0; upper_name[k]; k++)
    upper_name[k] = toupper((unsigned char)upper_name[k]);

  for (size_t i = 0; i < SUSPICIOUS_NAMES_COUNT; i++) {
    strncpy(upper_suspicious, SUSPICIOUS_NAMES[i], SKIMMER_NAME_MAX_LEN);
    upper_suspicious[SKIMMER_NAME_MAX_LEN] = '\0';
    for (int k = 0; upper_suspicious[k]; k++)
      upper_suspicious[k] = toupper((unsigned char)upper_suspicious[k]);

    if (strstr(upper_name, upper_suspicious) != NULL) {
      snprintf(reason_out, SKIMMER_REASON_MAX_LEN, "Module: %s", SUSPICIOUS_NAMES[i]);
      return;
    }
  }
  reason_out[0] = '\0';
}

static void scanner_callback(
    const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len) {
  struct ble_hs_adv_fields fields;
  if (ble_hs_adv_parse_fields(&fields, data, len) != 0)
    return;

  char name[SKIMMER_NAME_MAX_LEN] = {0};
  if (fields.name != NULL && fields.name_len > 0) {
    size_t nlen =
        fields.name_len < (SKIMMER_NAME_MAX_LEN - 1) ? fields.name_len : (SKIMMER_NAME_MAX_LEN - 1);
    memcpy(name, fields.name, nlen);
    name[nlen] = '\0';
  } else {
    return;
  }

  char reason[SKIMMER_REASON_MAX_LEN] = {0};
  check_suspicious(name, reason);

  if (strlen(reason) > 0) {
    if (xSemaphoreTake(s_list_mutex, pdMS_TO_TICKS(SKIMMER_MUTEX_TIMEOUT_MS)) == pdTRUE) {
      bool is_found = false;
      for (int i = 0; i < s_skimmer_count; i++) {
        if (memcmp(s_found_skimmers[i].addr.val, addr, 6) == 0) {
          s_found_skimmers[i].rssi = rssi;
          s_found_skimmers[i].last_seen = (uint32_t)(esp_timer_get_time() / USEC_PER_MSEC);
          is_found = true;
          break;
        }
      }

      if (!is_found && s_skimmer_count < SKIMMER_DETECTOR_MAX_RESULTS) {
        skimmer_detector_record_t *rec = &s_found_skimmers[s_skimmer_count];
        memcpy(rec->addr.val, addr, 6);
        rec->addr.type = addr_type;
        strncpy(rec->name, name, SKIMMER_NAME_MAX_LEN - 1);
        rec->name[SKIMMER_NAME_MAX_LEN - 1] = '\0';
        rec->rssi = rssi;
        strncpy(rec->detection_reason, reason, SKIMMER_REASON_MAX_LEN - 1);
        rec->last_seen = (uint32_t)(esp_timer_get_time() / USEC_PER_MSEC);

        s_skimmer_count++;
        ESP_LOGW(TAG, "Potential Skimmer: %s (%s)", name, reason);
      }
      xSemaphoreGive(s_list_mutex);
    }
  }
}

static void monitor_task(void *pvParameters) {
  bluetooth_service_start_sniffer(scanner_callback);

  while (s_is_running) {
    if (xSemaphoreTake(s_list_mutex, pdMS_TO_TICKS(SKIMMER_MUTEX_POLL_MS)) == pdTRUE) {
      uint32_t now = (uint32_t)(esp_timer_get_time() / USEC_PER_MSEC);
      for (int i = 0; i < s_skimmer_count;) {
        if ((now - s_found_skimmers[i].last_seen) > SKIMMER_TIMEOUT_MS) {
          if (i < s_skimmer_count - 1) {
            memmove(&s_found_skimmers[i],
                    &s_found_skimmers[i + 1],
                    sizeof(skimmer_detector_record_t) * (s_skimmer_count - 1 - i));
          }
          s_skimmer_count--;
        } else {
          i++;
        }
      }
      xSemaphoreGive(s_list_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(SKIMMER_POLL_DELAY_MS));
  }

  bluetooth_service_stop_sniffer();
  s_skimmer_task_handle = NULL;
  if (s_skimmer_task_stack != NULL) {
    free(s_skimmer_task_stack);
    s_skimmer_task_stack = NULL;
  }
  if (s_skimmer_task_tcb != NULL) {
    free(s_skimmer_task_tcb);
    s_skimmer_task_tcb = NULL;
  }
  vTaskDelete(NULL);
}
