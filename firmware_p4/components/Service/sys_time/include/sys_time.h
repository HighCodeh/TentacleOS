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

/**
 * @file sys_time.h
 * @brief Central wall-clock owner for TentacleOS.
 *
 * The HighBoy V2 has no battery-backed RTC, so wall-clock time is volatile
 * across power cycles: the P4's internal RTC timer runs while powered but resets
 * on power-off. Time must therefore be injected from an external source every
 * power cycle (console, companion app over host_link, or SNTP via the C5) and is
 * maintained by the internal timer until the next power loss.
 *
 * At boot the clock is seeded with a baseline so nothing is ever dated to 1970:
 * the firmware build date acts as a floor (the device cannot predate its own
 * image), raised to the last-known time persisted in NVS when that is newer. This
 * baseline is @ref SYS_TIME_STATE_ESTIMATED until a real source promotes it to
 * @ref SYS_TIME_STATE_SYNCED. The whole system runs in UTC.
 */

#ifndef SYS_TIME_H
#define SYS_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "esp_err.h"

/** @brief Trust level of the current wall-clock. */
typedef enum {
  SYS_TIME_STATE_ESTIMATED = 0, /**< Baseline (build date or last-known NVS); a floor, not a real sync. */
  SYS_TIME_STATE_SYNCED,        /**< Set from a real external source; time() is trustworthy. */
} sys_time_state_t;

/** @brief Origin of the current time value. */
typedef enum {
  SYS_TIME_SOURCE_NONE = 0,
  SYS_TIME_SOURCE_MANUAL, /**< Console `date` command. */
  SYS_TIME_SOURCE_HOST,   /**< Companion app over host_link. */
  SYS_TIME_SOURCE_SNTP,   /**< NTP over WiFi (via the C5). */
} sys_time_source_t;

/**
 * @brief Initialize the time service and seed the baseline clock.
 *
 * Fixes the system timezone to UTC and sets the clock to the later of the
 * firmware build date and the last-known time in NVS, leaving the state at
 * @ref SYS_TIME_STATE_ESTIMATED. Safe to call once at boot.
 */
void sys_time_init(void);

/**
 * @brief Apply a wall-clock time from a real source.
 *
 * Sets the system clock and marks it synced. Also persists the value to NVS as a
 * record of the last known time.
 *
 * @param epoch   Unix epoch seconds (UTC).
 * @param source  Where the value came from.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if @p epoch is implausibly early
 *   - ESP_FAIL if the clock could not be set
 */
esp_err_t sys_time_set(time_t epoch, sys_time_source_t source);

/** @brief Current trust level of the clock. */
sys_time_state_t sys_time_state(void);

/** @brief Source of the last successful sync. */
sys_time_source_t sys_time_source(void);

/** @brief Current wall-clock (UTC); at least the boot baseline. */
time_t sys_time_now(void);

/**
 * @brief Format the current time with strftime into @p out.
 *
 * @param[out] out       Destination buffer. Set to "" on error.
 * @param      out_size  Size of @p out.
 * @param      fmt       strftime format string (UTC fields).
 * @return true if the value was formatted, false on a bad argument.
 */
bool sys_time_format(char *out, size_t out_size, const char *fmt);

#ifdef __cplusplus
}
#endif

#endif // SYS_TIME_H
