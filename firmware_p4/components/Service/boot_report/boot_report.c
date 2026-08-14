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

#include "boot_report.h"

#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "BOOT_REPORT";

// Boot-loop counter in RTC_NOINIT memory: survives a reset but not a power
// cycle, and never writes flash. A magic word tells a real count from the
// garbage RTC RAM holds after power-on.
#define BOOTLOOP_MAGIC     0x7005B007u
#define STABLE_UPTIME_US   (20 * 1000 * 1000)  // clear the counter after 20 s up
#define NVS_NAMESPACE      "boot_report"
#define NVS_KEY_LAST_REASON "last_reason"
#define NVS_KEY_PANIC_TOTAL "panic_total"

static RTC_NOINIT_ATTR uint32_t s_rtc_magic;
static RTC_NOINIT_ATTR uint32_t s_rtc_abnormal_count;

static esp_timer_handle_t s_stable_timer = NULL;
static uint32_t s_panic_total = 0;

static boot_stage_t s_stages[BOOT_REPORT_MAX_STAGES];
static int s_stage_count = 0;
static bool s_required_ok = true;

static crash_info_t s_crash;
static bool s_crash_captured = false;

void boot_report_reset(void) {
  s_stage_count = 0;
  s_required_ok = true;
  memset(s_stages, 0, sizeof(s_stages));
}

void boot_report_record(const char *name, bool required, esp_err_t result) {
  if (result != ESP_OK) {
    ESP_LOGW(TAG, "stage '%s' (%s) -> %s", name, required ? "required" : "optional",
             esp_err_to_name(result));
  } else {
    ESP_LOGI(TAG, "stage '%s' ok", name);
  }

  if (required && result != ESP_OK) {
    s_required_ok = false;
  }

  if (s_stage_count >= BOOT_REPORT_MAX_STAGES) {
    return;  // map full: keep counting failures above but stop storing
  }

  s_stages[s_stage_count].name = name;
  s_stages[s_stage_count].required = required;
  s_stages[s_stage_count].result = result;
  s_stage_count++;
}

const boot_stage_t *boot_report_stages(int *out_count) {
  if (out_count != NULL) {
    *out_count = s_stage_count;
  }
  return s_stages;
}

bool boot_report_all_required_ok(void) {
  return s_required_ok;
}

const char *boot_report_reason_str(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_EXT:       return "External pin";
    case ESP_RST_SW:        return "Software restart";
    case ESP_RST_PANIC:     return "Panic / exception";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_USB:       return "USB peripheral";
    case ESP_RST_JTAG:      return "JTAG";
    case ESP_RST_EFUSE:     return "eFuse error";
    case ESP_RST_PWR_GLITCH: return "Power glitch";
    case ESP_RST_CPU_LOCKUP: return "CPU lockup";
    default:                return "Unknown";
  }
}

// An abnormal end that should feed boot-loop detection.
static bool reason_is_abnormal(esp_reset_reason_t reason) {
  return reason == ESP_RST_PANIC || reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT ||
         reason == ESP_RST_WDT || reason == ESP_RST_CPU_LOCKUP || reason == ESP_RST_BROWNOUT;
}

// Persist only the summary (last reason + running panic total). Called once per
// boot from capture_crash, which runs after nvs_flash_init. Writes on abnormal
// boots only, so flash wear is negligible.
static void persist_summary(esp_reset_reason_t reason, bool abnormal) {
  nvs_handle_t nvs;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
    return;
  }

  uint32_t total = 0;
  nvs_get_u32(nvs, NVS_KEY_PANIC_TOTAL, &total);

  if (abnormal) {
    total++;
    nvs_set_u32(nvs, NVS_KEY_PANIC_TOTAL, total);
    nvs_set_u32(nvs, NVS_KEY_LAST_REASON, (uint32_t)reason);
    nvs_commit(nvs);
  }

  s_panic_total = total;
  nvs_close(nvs);
}

void boot_report_capture_crash(void) {
  if (s_crash_captured) {
    return;
  }
  s_crash_captured = true;

  memset(&s_crash, 0, sizeof(s_crash));
  s_crash.reason = esp_reset_reason();

  // A valid image in the coredump partition means the previous run panicked.
  s_crash.has_coredump = (esp_core_dump_image_check() == ESP_OK);

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
  if (s_crash.has_coredump) {
    esp_core_dump_summary_t *summary = malloc(sizeof(esp_core_dump_summary_t));
    if (summary != NULL && esp_core_dump_get_summary(summary) == ESP_OK) {
      strlcpy(s_crash.task, summary->exc_task, sizeof(s_crash.task));
      s_crash.pc = summary->exc_pc;
      s_crash.mcause = summary->ex_info.mcause;
      s_crash.mtval = summary->ex_info.mtval;
      s_crash.ra = summary->ex_info.ra;
      s_crash.sp = summary->ex_info.sp;
    }
    free(summary);
  }
#endif

  s_crash.crash = s_crash.has_coredump || reason_is_abnormal(s_crash.reason);

  // NVS is up by now (kernel_init ran nvs_flash_init before this): keep a
  // persistent summary and cache the running panic total.
  persist_summary(s_crash.reason, s_crash.crash);

  if (s_crash.crash) {
    ESP_LOGW(TAG, "Previous run ended abnormally: %s (coredump=%d, task=%s)",
             boot_report_reason_str(s_crash.reason), s_crash.has_coredump, s_crash.task);
  }
}

bool boot_report_has_crash(void) {
  return s_crash_captured && s_crash.crash;
}

const crash_info_t *boot_report_crash(void) {
  return &s_crash;
}

esp_err_t boot_report_clear_crash(void) {
  esp_err_t ret = esp_core_dump_image_erase();
  if (ret == ESP_OK) {
    s_crash.has_coredump = false;
    s_crash.crash = false;
  } else {
    ESP_LOGW(TAG, "Core dump erase failed: %s", esp_err_to_name(ret));
  }
  return ret;
}

static void stable_timer_cb(void *arg) {
  (void)arg;
  boot_report_mark_stable();
}

void boot_report_track_bootloop(void) {
  esp_reset_reason_t reason = esp_reset_reason();

  // RTC RAM is undefined after a power cycle: a wrong magic means "fresh start".
  if (s_rtc_magic != BOOTLOOP_MAGIC) {
    s_rtc_magic = BOOTLOOP_MAGIC;
    s_rtc_abnormal_count = 0;
  }

  if (reason_is_abnormal(reason)) {
    s_rtc_abnormal_count++;
    ESP_LOGW(TAG, "Abnormal boot #%lu: %s", (unsigned long)s_rtc_abnormal_count,
             boot_report_reason_str(reason));
  } else {
    s_rtc_abnormal_count = 0;  // a clean reset / power-on breaks the loop
  }

  // Clear the counter once we have stayed up long enough to call this boot
  // stable, so a single crash does not accumulate toward the loop threshold.
  const esp_timer_create_args_t args = {
      .callback = stable_timer_cb,
      .name = "boot_stable",
  };
  if (esp_timer_create(&args, &s_stable_timer) == ESP_OK) {
    esp_timer_start_once(s_stable_timer, STABLE_UPTIME_US);
  }
}

bool boot_report_in_bootloop(void) {
  return s_rtc_magic == BOOTLOOP_MAGIC &&
         s_rtc_abnormal_count >= BOOT_REPORT_BOOTLOOP_THRESHOLD;
}

uint32_t boot_report_abnormal_boots(void) {
  return (s_rtc_magic == BOOTLOOP_MAGIC) ? s_rtc_abnormal_count : 0;
}

uint32_t boot_report_panic_total(void) {
  return s_panic_total;
}

void boot_report_mark_stable(void) {
  if (s_rtc_abnormal_count != 0) {
    ESP_LOGI(TAG, "Boot stable: clearing abnormal-boot counter (was %lu)",
             (unsigned long)s_rtc_abnormal_count);
  }
  s_rtc_abnormal_count = 0;
}
