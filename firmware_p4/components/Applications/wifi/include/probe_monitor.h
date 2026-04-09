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

#ifndef PROBE_MONITOR_H
#define PROBE_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Record for a captured probe request.
 */
typedef struct {
  uint8_t mac[6];
  int8_t rssi;
  char ssid[33];
  uint32_t last_seen_timestamp;
} probe_monitor_record_t;

/**
 * @brief Start the probe request monitor over SPI.
 *
 * @return true on success, false on failure.
 */
bool probe_monitor_start(void);

/**
 * @brief Stop the probe request monitor.
 */
void probe_monitor_stop(void);

/**
 * @brief Get cached probe monitor results, fetching new entries from C5.
 *
 * @param[out] out_count  Number of results.
 * @return Pointer to result array.
 */
probe_monitor_record_t *probe_monitor_get_results(uint16_t *out_count);

/**
 * @brief Free cached probe monitor results.
 */
void probe_monitor_free_results(void);

/**
 * @brief Save probe monitor results to internal flash via SPI.
 *
 * @return true on success, false on failure.
 */
bool probe_monitor_save_results_to_internal_flash(void);

/**
 * @brief Save probe monitor results to SD card via SPI.
 *
 * @return true on success, false on failure.
 */
bool probe_monitor_save_results_to_sd_card(void);

#ifdef __cplusplus
}
#endif

#endif // PROBE_MONITOR_H
