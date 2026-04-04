// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sys_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <string.h>
#include "kernel.h"

static const char *TAG = "SYS_MONITOR";

#define MONITOR_INTERVAL_MS 2000 
#define STACK_SIZE_BYTES    4096
#define CRITICAL_STACK_THRESHOLD 256

typedef struct {
  bool verbose_logging;
} sys_monitor_params_t;

static void sys_monitor_task(void *pvParameters) {
  sys_monitor_params_t *params = (sys_monitor_params_t *)pvParameters;
  bool verbose = params->verbose_logging;
  vPortFree(params);

  ESP_LOGI(TAG, "System Monitor (RAM & Stack) started. Verbose: %s", verbose ? "ENABLED" : "DISABLED");

  while (1) {
    if (verbose) {
      uint32_t free_heap = esp_get_free_heap_size();
      uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      uint32_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

      ESP_LOGI(TAG, "RAM Status - Total Free: %lu, Internal Free: %lu, PSRAM Free: %lu", 
               (unsigned long)free_heap, (unsigned long)internal_free, (unsigned long)spiram_free);
    }

    uint32_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *pxTaskStatusArray = pvPortMalloc(task_count * sizeof(TaskStatus_t));

    if (pxTaskStatusArray != NULL) {
      task_count = uxTaskGetSystemState(pxTaskStatusArray, task_count, NULL);

      for (uint32_t i = 0; i < task_count; i++) {
        uint32_t watermark = pxTaskStatusArray[i].usStackHighWaterMark;

        if (watermark < CRITICAL_STACK_THRESHOLD) {
          ESP_LOGE(TAG, "!!! SECURITY ALERT !!! Task [%s] has CRITICAL STACK: %lu bytes free. TERMINATING TASK.", 
                   pxTaskStatusArray[i].pcTaskName, (unsigned long)watermark);

          if (pxTaskStatusArray[i].xHandle != xTaskGetCurrentTaskHandle()) {
            vTaskDelete(pxTaskStatusArray[i].xHandle);
          }
        }
      }
      vPortFree(pxTaskStatusArray);
    }

    vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
  }
}

void sys_monitor(bool show_ram_logs) {
  sys_monitor_params_t *params = pvPortMalloc(sizeof(sys_monitor_params_t));
  if (params) {
    params->verbose_logging = show_ram_logs;

    xTaskCreatePinnedToCore(
      sys_monitor_task,   
      "SysMonitor",       
      STACK_SIZE_BYTES,   
      (void *)params,               
      1,                  
      NULL,               
      1                   
    );
  } else {
    ESP_LOGE(TAG, "Failed to allocate memory for SysMonitor parameters.");
  }
}
