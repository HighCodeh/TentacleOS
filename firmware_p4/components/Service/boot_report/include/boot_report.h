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

#ifndef BOOT_REPORT_H
#define BOOT_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_REPORT_MAX_STAGES 24

// One kernel_init subsystem and how it came up.
typedef struct {
  const char *name; ///< short subsystem label (static string)
  bool required;    ///< a required stage failing aborts to safe mode
  esp_err_t result; ///< ESP_OK, an error code, or ESP_ERR_NOT_FOUND when skipped
} boot_stage_t;

// Snapshot of the last reset / crash, filled once at boot.
typedef struct {
  esp_reset_reason_t reason; ///< why the previous run ended
  bool has_coredump;         ///< a valid core dump image was found in flash
  bool crash;                ///< reason or coredump indicates an abnormal end
  char task[16];             ///< faulting task name (coredump only)
  uint32_t pc;               ///< program counter at fault (coredump only)
  uint32_t mcause;           ///< RISC-V trap cause (coredump only)
  uint32_t mtval;            ///< RISC-V trap value (coredump only)
  uint32_t ra;               ///< return address (coredump only)
  uint32_t sp;               ///< stack pointer (coredump only)
} crash_info_t;

// Boot map (item 8)

/** @brief Clear the boot map. Call once at the very start of kernel_init. */
void boot_report_reset(void);

/**
 * @brief Record how a subsystem came up.
 * @param name      Static label string.
 * @param required  Whether a failure here should abort to safe mode.
 * @param result    Init return code (use ESP_ERR_NOT_FOUND for a skipped stage).
 */
void boot_report_record(const char *name, bool required, esp_err_t result);

/** @brief Access the recorded stages. @p out_count receives how many. */
const boot_stage_t *boot_report_stages(int *out_count);

/** @brief True while no required stage has reported a failure. */
bool boot_report_all_required_ok(void);

// Crash forensics (item 22)

/**
 * @brief Capture the last reset reason and, if present, the core dump summary.
 *
 * Call once early in boot. Reads esp_reset_reason() and esp_core_dump_get_summary
 * so the info survives even after the dump image is later cleared.
 */
void boot_report_capture_crash(void);

/** @brief True if the previous run ended in a panic/watchdog or left a dump. */
bool boot_report_has_crash(void);

/** @brief The captured crash/reset snapshot (valid after capture). */
const crash_info_t *boot_report_crash(void);

/** @brief Erase the core dump image from flash. */
esp_err_t boot_report_clear_crash(void);

/** @brief Human-readable name for a reset reason. */
const char *boot_report_reason_str(esp_reset_reason_t reason);

// Boot-loop detection (item 4)

/** @brief Number of consecutive abnormal boots that trips degraded mode. */
#define BOOT_REPORT_BOOTLOOP_THRESHOLD 3

/**
 * @brief Update the consecutive-abnormal-boot counter. Call FIRST in app_main.
 *
 * Reads esp_reset_reason() and keeps a counter in RTC_NOINIT memory (survives a
 * reset but not a power cycle, so it never touches flash). An abnormal reason
 * (panic / watchdog / brownout / lockup) increments it; a clean reset or a
 * fresh power-on clears it. Also arms a one-shot timer that clears the counter
 * once the device has been up long enough to be considered stable.
 */
void boot_report_track_bootloop(void);

/** @brief True when the abnormal-boot counter has reached the threshold. */
bool boot_report_in_bootloop(void);

/** @brief Current consecutive-abnormal-boot count. */
uint32_t boot_report_abnormal_boots(void);

/** @brief Total panics recorded across power cycles (persisted in NVS). */
uint32_t boot_report_panic_total(void);

/** @brief Total boots recorded across power cycles (persisted in NVS). */
uint32_t boot_report_boot_count(void);

/** @brief Clear the abnormal-boot counter now (a stable boot was reached). */
void boot_report_mark_stable(void);

#ifdef __cplusplus
}
#endif

#endif // BOOT_REPORT_H
