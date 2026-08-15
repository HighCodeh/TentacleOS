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

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sys_prio.h"

#include "assets_manager.h"
#include "i2c_init.h"
#include "kernel.h"
#include "storage_init.h"
#include "ui_liveness.h"
#include "header_ui.h"

static const char *TAG = "SYS_MONITOR";

#define MONITOR_INTERVAL_MS      2000
#define STORAGE_CHECK_CYCLES     30    // storage health probe cadence (cycles x interval = ~60 s)
#define MONITOR_STACK_SIZE       4096
#define MONITOR_PRIORITY SYS_PRIO_MONITOR
#define MONITOR_CORE SYS_CORE_RADIO
#define CRITICAL_STACK_THRESHOLD 256   // free stack (bytes); below this a task is at risk
#define STACK_ESCALATE_CYCLES    5     // consecutive critical cycles before a controlled restart
#define STACK_WATCH_MAX          8     // distinct critical tasks tracked for persistence
// Consecutive cycles (2 s each) with no render progress before a controlled
// restart. Must sit well above the longest legitimate renderer stall: blocking
// console/app operations (e.g. `wifi scan`, which runs a multi-second blocking
// scan while the home screen still queries Wi-Fi under the LVGL lock) freeze the
// renderer for a few seconds without being deadlocked. Only a genuinely stuck UI
// should reboot, so this is intentionally generous (~16 s).
#define UI_STALL_ESCALATE_CYCLES 8
#define REBOOT_GRACE_MS          1500  // let the UI alert render and logs flush before restart
#define ALERT_MSG_SIZE           128

// Internal-RAM heap policy. The largest contiguous block matters as much as the
// total free: LVGL fragments the heap opening/closing screens, so a screen can
// fail to allocate long before the total runs out.
#define HEAP_WARN_FREE_B    24576  // total internal free below this -> warn once
#define HEAP_WARN_LARGEST_B 12288  // largest contiguous block below this -> warn once
#define HEAP_CRIT_FREE_B    8192   // sustained below this -> controlled restart
#define HEAP_CRIT_CYCLES    3

typedef struct {
  bool is_verbose;
} sys_monitor_params_t;

// The monitor observes and reports; it never deletes tasks. Deleting a task
// mid-transaction leaks the peripheral bus mutex (I2C/SPI) and wedges the driver
// until reboot, turning a tight stack into a dead peripheral. When a task stays
// critically low for STACK_ESCALATE_CYCLES consecutive cycles we do a controlled
// restart instead. usStackHighWaterMark is monotonic (it records the lowest free
// stack ever seen), so a persistent streak means the task is alive and running
// with dangerously little headroom, not a past transient.
typedef struct {
  char name[configMAX_TASK_NAME_LEN];
  uint32_t streak;  // consecutive monitor cycles this task has been critical
  bool alerted;     // UI warning already shown for the current streak
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

// Single escalation path shared by every health check: warn, give the alert a
// moment to render and the logs to flush, then restart. It does not return.
static void controlled_restart(const char *title, const char *message) {
  safeguard_alert(title, message);
  vTaskDelay(pdMS_TO_TICKS(REBOOT_GRACE_MS));
  esp_restart();
}

static void escalate_stack_restart(const char *name, uint32_t watermark) {
  ESP_LOGE(TAG,
           "Task [%s] critically low on stack (%lu B) for %d cycles; controlled restart",
           name,
           (unsigned long)watermark,
           STACK_ESCALATE_CYCLES);

  char msg_buf[ALERT_MSG_SIZE];
  snprintf(msg_buf, sizeof(msg_buf), "Low stack in '%s'\npersisted. Restarting\nto recover.", name);
  controlled_restart("SYSTEM RECOVERY", msg_buf);
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

    // Warn on the UI once per streak (safeguard_alert takes the LVGL lock).
    if (!w->alerted) {
      w->alerted = true;
      char msg_buf[ALERT_MSG_SIZE];
      snprintf(msg_buf,
               sizeof(msg_buf),
               "Low stack in '%s'\n(%lu B free). Watching.",
               name,
               (unsigned long)watermark);
      safeguard_alert("LOW STACK", msg_buf);
    }

    if (w->streak >= STACK_ESCALATE_CYCLES) {
      escalate_stack_restart(name, watermark);  // does not return
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

// UI liveness. The LVGL renderer runs in a managed task that cannot be watched
// by name, and its stack watermark says nothing about progress. Instead we poll
// ui_render_beat(), a counter bumped by an lv_timer inside that task. We arm
// only after the beat first advances (UI is up), then a beat that does not move
// for UI_STALL_ESCALATE_CYCLES cycles means the renderer is frozen (lock
// deadlock, runaway callback, or a dead task) and gets the same controlled
// restart as a critical stack. No task-name matching anywhere.
static void check_ui_liveness(void) {
  static bool armed = false;
  static uint32_t last_beat = 0;
  static uint32_t stall_streak = 0;

  uint32_t beat = ui_render_beat();

  if (!armed) {
    if (beat != 0) {
      armed = true;
      last_beat = beat;
    }
    return;
  }

  if (beat != last_beat) {
    last_beat = beat;
    stall_streak = 0;
    return;
  }

  if (++stall_streak >= UI_STALL_ESCALATE_CYCLES) {
    ESP_LOGE(TAG, "LVGL renderer stalled for %d cycles (no progress); controlled restart",
             UI_STALL_ESCALATE_CYCLES);
    controlled_restart("SYSTEM RECOVERY", "UI renderer stalled.\nRestarting to recover.");
  }
}

static void check_heap(void) {
  static bool warned = false;
  static uint32_t crit_streak = 0;

  uint32_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

  if (free_int < HEAP_WARN_FREE_B || largest < HEAP_WARN_LARGEST_B) {
    if (!warned) {
      warned = true;
      ESP_LOGW(TAG,
               "Low heap: %lu B free, largest block %lu B (min ever %lu B)",
               (unsigned long)free_int,
               (unsigned long)largest,
               (unsigned long)esp_get_minimum_free_heap_size());
      assets_manager_evict_cache();  // reclaim the image cache before alerting
      char msg[ALERT_MSG_SIZE];
      snprintf(msg,
               sizeof(msg),
               "Low memory:\n%lu KB free,\n%lu KB block.",
               (unsigned long)(free_int / 1024),
               (unsigned long)(largest / 1024));
      safeguard_alert("LOW MEMORY", msg);
    }
  } else {
    warned = false;
  }

  if (free_int < HEAP_CRIT_FREE_B) {
    if (++crit_streak >= HEAP_CRIT_CYCLES) {
      ESP_LOGE(TAG, "Heap critically low (%lu B) for %d cycles; controlled restart",
               (unsigned long)free_int, HEAP_CRIT_CYCLES);
      controlled_restart("SYSTEM RECOVERY", "Out of memory.\nRestarting to recover.");
    }
  } else {
    crit_streak = 0;
  }
}

static void check_storage_health(void) {
  if (!storage_is_mounted())
    return;
  if (storage_check_health() != ESP_OK) {
    ESP_LOGW(TAG, "SD health check failed; requesting remount");
    header_ui_request_sd_remount();
  }
}

static void check_i2c_health(void) {
  static uint32_t last_recover = 0;
  uint32_t now = i2c_recover_count();
  if (now != last_recover) {
    ESP_LOGW(TAG, "I2C bus recovered (total %lu)", (unsigned long)now);
    last_recover = now;
  }
}

static void sys_monitor_task(void *pvParameters) {
  sys_monitor_params_t *params = (sys_monitor_params_t *)pvParameters;
  bool is_verbose = params->is_verbose;
  vPortFree(params);

  ESP_LOGI(TAG, "System monitor started (verbose: %s)", is_verbose ? "enabled" : "disabled");

  // The monitor is the single supervisor: it feeds the Task Watchdog (so a stuck
  // monitor or a core-starving task panic-reboots), watches every task's stack,
  // and watches UI render liveness. Same role as on the C5, minus the UI check.
  esp_task_wdt_add(NULL);

  uint32_t storage_cycle = 0;

  while (1) {
    esp_task_wdt_reset();

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
    TaskStatus_t *task_array = pvPortMalloc(task_count * sizeof(TaskStatus_t));

    if (task_array != NULL) {
      task_count = uxTaskGetSystemState(task_array, task_count, NULL);
      check_task_stacks(task_array, task_count);
      vPortFree(task_array);
    } else {
      ESP_LOGE(TAG, "Failed to allocate task status array");
    }

    check_ui_liveness();
    check_heap();
    check_i2c_health();

    if (++storage_cycle >= STORAGE_CHECK_CYCLES) {
      storage_cycle = 0;
      check_storage_health();
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
