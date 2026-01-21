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

#include "skimmer_detector.h"
#include "bluetooth_service.h"
#include "host/ble_hs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "SKIMMER_DETECTOR";

#define SKIMMER_TASK_STACK_SIZE 4096
#define SKIMMER_TIMEOUT_MS 30000

static skimmer_record_t *found_skimmers = NULL;
static uint16_t skimmer_count = 0;
static bool is_running = false;

static TaskHandle_t skimmer_task_handle = NULL;
static StackType_t *skimmer_task_stack = NULL;
static StaticTask_t *skimmer_task_tcb = NULL;
static SemaphoreHandle_t list_mutex = NULL;

static const char *suspicious_names[] = {
    "HC-05", "HC-06", "HC-08", 
    "HM-10", "HM-Soft", 
    "CC41", "MLT-BT05", 
    "JDY-", "AT-09", 
    "BT05", "SPP-CA"
};
static const int suspicious_names_count = sizeof(suspicious_names) / sizeof(suspicious_names[0]);

static void is_suspicious_check(const char *device_name, char *reason_out) {
    if (strlen(device_name) == 0) return;

    char upper_name[33];
    char upper_suspicious[33];
    
    strncpy(upper_name, device_name, 32);
    upper_name[32] = '\0';
    for(int k=0; upper_name[k]; k++) upper_name[k] = toupper((unsigned char)upper_name[k]);

    for (int i = 0; i < suspicious_names_count; i++) {
        strncpy(upper_suspicious, suspicious_names[i], 32);
        upper_suspicious[32] = '\0';
        for(int k=0; upper_suspicious[k]; k++) upper_suspicious[k] = toupper((unsigned char)upper_suspicious[k]);

        if (strstr(upper_name, upper_suspicious) != NULL) {
            snprintf(reason_out, 32, "Module: %s", suspicious_names[i]);
            return;
        }
    }
    reason_out[0] = '\0';
}

static void scanner_callback(const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len) {
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, data, len) != 0) return;

    char name[32] = {0};
    if (fields.name != NULL && fields.name_len > 0) {
        size_t nlen = fields.name_len < 31 ? fields.name_len : 31;
        memcpy(name, fields.name, nlen);
        name[nlen] = '\0';
    } else {
        return; 
    }

    char reason[32] = {0};
    is_suspicious_check(name, reason);

    if (strlen(reason) > 0) {
        if (xSemaphoreTake(list_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool found = false;
            for (int i = 0; i < skimmer_count; i++) {
                if (memcmp(found_skimmers[i].addr.val, addr, 6) == 0) {
                    found_skimmers[i].rssi = rssi;
                    found_skimmers[i].last_seen = (uint32_t)(esp_timer_get_time() / 1000);
                    found = true;
                    break;
                }
            }

            if (!found && skimmer_count < MAX_SKIMMERS_FOUND) {
                skimmer_record_t *rec = &found_skimmers[skimmer_count];
                memcpy(rec->addr.val, addr, 6);
                rec->addr.type = addr_type;
                strncpy(rec->name, name, 31);
                rec->name[31] = '\0';
                rec->rssi = rssi;
                strncpy(rec->detection_reason, reason, 31);
                rec->last_seen = (uint32_t)(esp_timer_get_time() / 1000);
                
                skimmer_count++;
                ESP_LOGW(TAG, "Potential Skimmer: %s (%s)", name, reason);
            }
            xSemaphoreGive(list_mutex);
        }
    }
}

static void skimmer_monitor_task(void *pvParameters) {
    bluetooth_service_start_sniffer(scanner_callback);

    while (is_running) {
        if (xSemaphoreTake(list_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            for (int i = 0; i < skimmer_count; ) {
                if ((now - found_skimmers[i].last_seen) > SKIMMER_TIMEOUT_MS) {
                    if (i < skimmer_count - 1) {
                        memmove(&found_skimmers[i], &found_skimmers[i+1], sizeof(skimmer_record_t) * (skimmer_count - 1 - i));
                    }
                    skimmer_count--;
                } else {
                    i++;
                }
            }
            xSemaphoreGive(list_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    bluetooth_service_stop_sniffer();
    skimmer_task_handle = NULL;
    if (skimmer_task_stack) { heap_caps_free(skimmer_task_stack); skimmer_task_stack = NULL; }
    if (skimmer_task_tcb) { heap_caps_free(skimmer_task_tcb); skimmer_task_tcb = NULL; }
    vTaskDelete(NULL);
}

bool skimmer_detector_start(void) {
    if (is_running) return false;
    if (list_mutex == NULL) list_mutex = xSemaphoreCreateMutex();
    if (found_skimmers == NULL) {
        found_skimmers = (skimmer_record_t *)heap_caps_malloc(MAX_SKIMMERS_FOUND * sizeof(skimmer_record_t), MALLOC_CAP_SPIRAM);
        if (!found_skimmers) return false;
    }
    is_running = true;
    skimmer_task_stack = (StackType_t *)heap_caps_malloc(SKIMMER_TASK_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    skimmer_task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_SPIRAM);
    if (!skimmer_task_stack || !skimmer_task_tcb) {
        if (skimmer_task_stack) heap_caps_free(skimmer_task_stack);
        if (skimmer_task_tcb) heap_caps_free(skimmer_task_tcb);
        is_running = false;
        return false;
    }
    skimmer_task_handle = xTaskCreateStatic(skimmer_monitor_task, "skimmer_task", SKIMMER_TASK_STACK_SIZE, NULL, 5, skimmer_task_stack, skimmer_task_tcb);
    return (skimmer_task_handle != NULL);
}

void skimmer_detector_stop(void) {
    is_running = false;
}

skimmer_record_t* skimmer_detector_get_results(uint16_t *count) {
    *count = skimmer_count;
    return found_skimmers;
}

void skimmer_detector_clear_results(void) {
    if (xSemaphoreTake(list_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        skimmer_count = 0;
        xSemaphoreGive(list_mutex);
    }
}
