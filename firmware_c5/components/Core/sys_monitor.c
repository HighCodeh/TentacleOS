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

#include "sys_monitor.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sys_prio.h"

#include "kernel.h"

static const char *TAG = "SYS_MONITOR";

#define MONITOR_INTERVAL_MS      2000
#define STACK_SIZE_BYTES         4096
#define MONITOR_PRIORITY         SYS_PRIO_MONITOR
#define MONITOR_CORE             SYS_CORE_MAIN
#define CRITICAL_STACK_THRESHOLD 256   // free stack (bytes); below this a task is at risk
#define STACK_ESCALATE_CYCLES    5     // consecutive critical cycles before a controlled restart
#define STACK_WATCH_MAX          8     // distinct critical tasks tracked for persistence
#define REBOOT_GRACE_MS          1500  // let the alert log flush before restart
#define ALERT_MSG_SIZE           128

typedef struct {
  bool verbose_logging;
} sys_monitor_params_t;

// The monitor observes and reports; it never deletes tasks. Deleting a task
// mid-transaction leaks the peripheral bus mutex (I2C/SPI) and wedges the driver
// until reboot, turning a tight stack into a dead peripheral. On the C5 the SPI
// bridge to the P4 is the worst thing to strand this way. When a task stays
// critically low for STACK_ESCALATE_CYCLES consecutive cycles we do a controlled
// restart instead. usStackHighWaterMark is monotonic (it records the lowest free
// stack ever seen), so a persistent streak means the task is alive and running
// with dangerously little headroom, not a past transient.
//
// The C5 is headless, so "report" means logging: safeguard_alert() just logs.
typedef struct {
  char name[configMAX_TASK_NAME_LEN];
  uint32_t streak;  // consecutive monitor cycles this task has been critical
  bool alerted;     // alert already emitted for the current streak
  bool seen;        // matched in the cycle currently being processed
} stack_watch_t;

static stack_watch_t s_watch[STACK_WATCH_MAX];
static uint32_t s_watch_count;

static stack_watch_t *watch_find(const char *name) {
  for (uint32_t i = 0; i < s_watch_count; i++) {
    if (strcmp(s_watch[i].name, name) == 0) {
      return &s_watch[i];
    }
  }
  return NULL;
}

static void escalate_restart(const char *name, uint32_t watermark) {
  ESP_LOGE(TAG,
           "Task [%s] critically low on stack (%lu B) for %d cycles; controlled restart",
           name,
           (unsigned long)watermark,
           STACK_ESCALATE_CYCLES);

  char msg_buf[ALERT_MSG_SIZE];
  snprintf(msg_buf, sizeof(msg_buf), "Low stack in '%s' persisted; restarting to recover", name);
  safeguard_alert("SYSTEM RECOVERY", msg_buf);

  // TODO(item 31): call the graceful shutdown hook here once it exists so the
  // filesystem and radios flush their state before the restart.
  vTaskDelay(pdMS_TO_TICKS(REBOOT_GRACE_MS));  // let the alert log flush
  esp_restart();
}

static void check_task_stacks(const TaskStatus_t *tasks, uint32_t count) {
  for (uint32_t i = 0; i < s_watch_count; i++) {
    s_watch[i].seen = false;
  }

  for (uint32_t i = 0; i < count; i++) {
    uint32_t watermark = tasks[i].usStackHighWaterMark;
    if (watermark >= CRITICAL_STACK_THRESHOLD) {
      continue;
    }

    const char *name = tasks[i].pcTaskName;
    ESP_LOGW(TAG,
             "Low stack in task [%s]: %lu B free (threshold %d B)",
             name,
             (unsigned long)watermark,
             CRITICAL_STACK_THRESHOLD);

    stack_watch_t *w = watch_find(name);
    if (w == NULL) {
      if (s_watch_count >= STACK_WATCH_MAX) {
        continue;  // table full; the condition is still logged above
      }
      w = &s_watch[s_watch_count++];
      strncpy(w->name, name, sizeof(w->name) - 1);
      w->name[sizeof(w->name) - 1] = '\0';
      w->streak = 0;
      w->alerted = false;
    }

    w->seen = true;
    w->streak++;

    // Report once per streak (log-only on the headless C5).
    if (!w->alerted) {
      w->alerted = true;
      char msg_buf[ALERT_MSG_SIZE];
      snprintf(
          msg_buf, sizeof(msg_buf), "Low stack in '%s' (%lu B free)", name, (unsigned long)watermark);
      safeguard_alert("LOW STACK", msg_buf);
    }

    if (w->streak >= STACK_ESCALATE_CYCLES) {
      escalate_restart(name, watermark);  // does not return
    }
  }

  // Forget tasks that are no longer critical or have exited, so a fresh dip
  // starts a new streak instead of inheriting a stale one.
  uint32_t kept = 0;
  for (uint32_t i = 0; i < s_watch_count; i++) {
    if (s_watch[i].seen) {
      s_watch[kept++] = s_watch[i];
    }
  }
  s_watch_count = kept;
}

static void sys_monitor_task(void *pvParameters) {
  sys_monitor_params_t *params = (sys_monitor_params_t *)pvParameters;
  bool verbose = params->verbose_logging;
  vPortFree(params);

  ESP_LOGI(
      TAG, "System Monitor (RAM & Stack) started. Verbose: %s", verbose ? "ENABLED" : "DISABLED");

  // The monitor loop is the system health heartbeat (mirrors the P4 UI task):
  // it subscribes to the Task Watchdog and feeds it each cycle. With
  // CONFIG_ESP_TASK_WDT_PANIC=y a stuck monitor, or a task that starves the
  // single core for longer than the timeout, reboots instead of only warning.
  esp_task_wdt_add(NULL);

  while (1) {
    esp_task_wdt_reset();

    if (verbose) {
      uint32_t free_heap = esp_get_free_heap_size();
      uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      uint32_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

      ESP_LOGI(TAG,
               "RAM Status - Total Free: %lu, Internal Free: %lu, PSRAM Free: %lu",
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

void sys_monitor(bool show_ram_logs) {
  sys_monitor_params_t *params = pvPortMalloc(sizeof(sys_monitor_params_t));
  if (params) {
    params->verbose_logging = show_ram_logs;

    xTaskCreatePinnedToCore(sys_monitor_task,
                            "SysMonitor",
                            STACK_SIZE_BYTES,
                            (void *)params,
                            MONITOR_PRIORITY,
                            NULL,
                            MONITOR_CORE);
  } else {
    ESP_LOGE(TAG, "Failed to allocate memory for SysMonitor parameters.");
  }
}
