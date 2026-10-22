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

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
#define MONITOR_STACK_SIZE       4096
#define CRITICAL_STACK_THRESHOLD 256  // free stack (bytes); below this a task is at risk
#define STACK_ESCALATE_CYCLES    5    // consecutive critical cycles before a controlled restart
#define STACK_WATCH_MAX          8    // distinct critical tasks tracked for persistence
#define REBOOT_GRACE_MS          1500 // let the alert log flush before restart
#define ALERT_MSG_SIZE           128

// Internal-RAM heap policy (the tight pool; wifi/bt buffers + task stacks live
// here). The largest contiguous block is tracked alongside total free because
// fragmentation can fail an allocation before the total runs out.
#define HEAP_WARN_FREE_B    24576 // total internal free below this -> warn once
#define HEAP_WARN_LARGEST_B 12288 // largest contiguous block below this -> warn once
#define HEAP_CRIT_FREE_B    8192  // sustained below this -> controlled restart
#define HEAP_CRIT_CYCLES    3

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
  TaskHandle_t task;
  UBaseType_t number;
  char name[configMAX_TASK_NAME_LEN];
  uint32_t streak; // consecutive monitor cycles this task has been critical
  bool alerted;    // alert already emitted for the current streak
  bool seen;       // matched in the cycle currently being processed
} sys_monitor_stack_watch_t;

static sys_monitor_stack_watch_t s_watch[STACK_WATCH_MAX];
static uint32_t s_watch_count;
static atomic_bool s_is_started;

static sys_monitor_stack_watch_t *watch_find(const TaskStatus_t *task) {
  for (uint32_t i = 0; i < s_watch_count; i++) {
    if (s_watch[i].task == task->xHandle && s_watch[i].number == task->xTaskNumber) {
      return &s_watch[i];
    }
  }
  return NULL;
}

// Panic recovery retains abnormal-reset accounting and the configured core dump.
static void controlled_restart(const char *title, const char *message) {
  safeguard_alert(title, message);
  vTaskDelay(pdMS_TO_TICKS(REBOOT_GRACE_MS));
  abort();
}

static void escalate_stack_restart(const char *name, uint32_t watermark) {
  ESP_LOGE(TAG,
           "Task [%s] critically low on stack (%lu B) for %d cycles; controlled restart",
           name,
           (unsigned long)watermark,
           STACK_ESCALATE_CYCLES);

  char msg_buf[ALERT_MSG_SIZE];
  snprintf(msg_buf, sizeof(msg_buf), "Low stack in '%s' persisted; restarting to recover", name);
  controlled_restart("SYSTEM RECOVERY", msg_buf);
}

static bool check_task_stacks(const TaskStatus_t *tasks, uint32_t count) {
  bool is_healthy = true;
  uint32_t retained = 0;
  for (uint32_t i = 0; i < s_watch_count; ++i) {
    for (uint32_t j = 0; j < count; ++j) {
      if (s_watch[i].task == tasks[j].xHandle && s_watch[i].number == tasks[j].xTaskNumber &&
          tasks[j].usStackHighWaterMark < CRITICAL_STACK_THRESHOLD) {
        s_watch[i].seen = false;
        s_watch[retained++] = s_watch[i];
        break;
      }
    }
  }
  s_watch_count = retained;

  for (uint32_t i = 0; i < count; i++) {
    uint32_t watermark = tasks[i].usStackHighWaterMark;
    if (watermark >= CRITICAL_STACK_THRESHOLD) {
      continue;
    }

    is_healthy = false;
    const char *name = tasks[i].pcTaskName;
    ESP_LOGW(TAG,
             "Low stack in task [%s]: %lu B free (threshold %d B)",
             name,
             (unsigned long)watermark,
             CRITICAL_STACK_THRESHOLD);

    sys_monitor_stack_watch_t *w = watch_find(&tasks[i]);
    if (w == NULL) {
      if (s_watch_count >= STACK_WATCH_MAX) {
        controlled_restart("SYSTEM RECOVERY", "Too many tasks with critical stack headroom");
      }
      w = &s_watch[s_watch_count++];
      w->task = tasks[i].xHandle;
      w->number = tasks[i].xTaskNumber;
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
      snprintf(msg_buf,
               sizeof(msg_buf),
               "Low stack in '%s' (%lu B free)",
               name,
               (unsigned long)watermark);
      safeguard_alert("LOW STACK", msg_buf);
    }

    if (w->streak >= STACK_ESCALATE_CYCLES) {
      escalate_stack_restart(name, watermark); // does not return
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
  return is_healthy;
}

static bool check_heap(void) {
  static bool warned = false;
  static uint32_t crit_streak = 0;

  uint32_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

  if (free_int < HEAP_WARN_FREE_B || largest < HEAP_WARN_LARGEST_B) {
    if (!warned) {
      warned = true;
      ESP_LOGW(TAG,
               "Low internal heap: %lu B free, largest block %lu B (min internal %lu B)",
               (unsigned long)free_int,
               (unsigned long)largest,
               (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
      // Do not compare an internal-RAM warning with the total/default heap's
      // historical minimum: that pool also includes the much larger PSRAM.
      const uint32_t external_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
      ESP_LOGW(TAG,
               "PSRAM heap: %lu B free, largest block %lu B (min PSRAM %lu B)",
               (unsigned long)heap_caps_get_free_size(external_caps),
               (unsigned long)heap_caps_get_largest_free_block(external_caps),
               (unsigned long)heap_caps_get_minimum_free_size(external_caps));
      char msg[ALERT_MSG_SIZE];
      snprintf(msg,
               sizeof(msg),
               "Low memory: %lu KB free, %lu KB block",
               (unsigned long)(free_int / 1024),
               (unsigned long)(largest / 1024));
      safeguard_alert("LOW MEMORY", msg);
    }
  } else {
    warned = false;
  }

  if (free_int < HEAP_CRIT_FREE_B) {
    if (++crit_streak >= HEAP_CRIT_CYCLES) {
      ESP_LOGE(TAG,
               "Heap critically low (%lu B) for %d cycles; controlled restart",
               (unsigned long)free_int,
               HEAP_CRIT_CYCLES);
      controlled_restart("SYSTEM RECOVERY", "Out of memory; restarting to recover");
    }
  } else {
    crit_streak = 0;
  }
  return free_int >= HEAP_WARN_FREE_B && largest >= HEAP_WARN_LARGEST_B;
}

static void monitor_task(void *pvParameters) {
  bool is_verbose = (uintptr_t)pvParameters != 0;

  ESP_LOGI(TAG, "System monitor started (verbose: %s)", is_verbose ? "enabled" : "disabled");

  // The monitor loop is the system health heartbeat (mirrors the P4 UI task):
  // it subscribes to the Task Watchdog and feeds it each cycle. With
  // CONFIG_ESP_TASK_WDT_PANIC=y a stuck monitor, or a task that starves the
  // single core for longer than the timeout, reboots instead of only warning.
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  while (1) {
    ESP_ERROR_CHECK(esp_task_wdt_reset());

    if (is_verbose) {
      uint32_t free_heap = esp_get_free_heap_size();
      uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      uint32_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

      ESP_LOGI(TAG,
               "RAM — Free: %lu, Internal: %lu, PSRAM: %lu",
               (unsigned long)free_heap,
               (unsigned long)internal_free,
               (unsigned long)spiram_free);
    }

    uint32_t task_count = uxTaskGetNumberOfTasks();
    bool is_healthy = false;
    TaskStatus_t *task_array = NULL;
    if (task_count > 0 && sizeof(TaskStatus_t) <= SIZE_MAX / task_count) {
      task_array = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    }

    if (task_array != NULL) {
      task_count = uxTaskGetSystemState(task_array, task_count, NULL);
      is_healthy = task_count > 0 && check_task_stacks(task_array, task_count);
      vPortFree(task_array);
    } else {
      ESP_LOGE(TAG, "Failed to allocate task status array");
    }

    (void)is_healthy;
    check_heap();

    vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
  }
}

esp_err_t sys_monitor_start(bool is_verbose) {
  bool is_started = false;
  if (!atomic_compare_exchange_strong(&s_is_started, &is_started, true)) {
    return ESP_OK;
  }
  if (xTaskCreatePinnedToCore(monitor_task,
                              "SysMonitor",
                              MONITOR_STACK_SIZE,
                              (void *)(uintptr_t)is_verbose,
                              SYS_PRIO_MONITOR,
                              NULL,
                              SYS_CORE_MAIN) != pdPASS) {
    atomic_store(&s_is_started, false);
    ESP_LOGE(TAG, "Failed to create system monitor");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
