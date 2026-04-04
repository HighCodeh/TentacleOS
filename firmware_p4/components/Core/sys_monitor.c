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

#include "sys_monitor.h"

#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "kernel.h"
#include "ui_manager.h"

static const char *TAG = "SYS_MONITOR";

#define MONITOR_INTERVAL_MS       2000
#define MONITOR_STACK_SIZE        4096
#define MONITOR_PRIORITY          1
#define MONITOR_CORE              1
#define CRITICAL_STACK_THRESHOLD  256
#define ALERT_MSG_SIZE            128

typedef struct {
  bool is_verbose;
} sys_monitor_params_t;

static void check_task_stacks(TaskStatus_t *tasks, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    uint32_t watermark = tasks[i].usStackHighWaterMark;

    if (watermark >= CRITICAL_STACK_THRESHOLD) {
      continue;
    }

    ESP_LOGE(TAG, "CRITICAL STACK in task [%s]: %lu bytes free",
             tasks[i].pcTaskName, (unsigned long)watermark);

    // UI Task requires special recovery
    if (strcmp(tasks[i].pcTaskName, "UI Task") == 0) {
      ESP_LOGE(TAG, "UI Task crash — initiating emergency restart");
      vTaskDelete(tasks[i].xHandle);
      ui_hard_restart();
      continue;
    }

    ui_switch_screen(SCREEN_HOME);

    char msg_buf[ALERT_MSG_SIZE];
    snprintf(msg_buf, sizeof(msg_buf),
             "Process '%s' Terminated!\nLow Stack (%lu B).",
             tasks[i].pcTaskName, (unsigned long)watermark);

    safeguard_alert("SYSTEM PROTECTED", msg_buf);

    if (tasks[i].xHandle != xTaskGetCurrentTaskHandle()) {
      vTaskDelete(tasks[i].xHandle);
    }
  }
}

static void sys_monitor_task(void *pvParameters) {
  sys_monitor_params_t *params = (sys_monitor_params_t *)pvParameters;
  bool is_verbose = params->is_verbose;
  vPortFree(params);

  ESP_LOGI(TAG, "System monitor started (verbose: %s)",
           is_verbose ? "enabled" : "disabled");

  while (1) {
    if (is_verbose) {
      uint32_t free_heap = esp_get_free_heap_size();
      uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      uint32_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

      ESP_LOGI(TAG, "RAM — Free: %lu, Internal: %lu, PSRAM: %lu",
               (unsigned long)free_heap,
               (unsigned long)internal_free,
               (unsigned long)spiram_free);
    }

    uint32_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_array = pvPortMalloc(task_count * sizeof(TaskStatus_t));

    if (task_array != NULL) {
      task_count = uxTaskGetSystemState(task_array, task_count, NULL);
      check_task_stacks(task_array, task_count);
      vPortFree(task_array);
    } else {
      ESP_LOGE(TAG, "Failed to allocate task status array");
    }

    vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
  }
}

void sys_monitor_start(bool is_verbose) {
  sys_monitor_params_t *params = pvPortMalloc(sizeof(sys_monitor_params_t));
  if (params == NULL) {
    ESP_LOGE(TAG, "Failed to allocate monitor parameters");
    return;
  }

  params->is_verbose = is_verbose;

  xTaskCreatePinnedToCore(sys_monitor_task,
                          "SysMonitor",
                          MONITOR_STACK_SIZE,
                          (void *)params,
                          MONITOR_PRIORITY,
                          NULL,
                          MONITOR_CORE);
}
