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

#ifndef FREQUENCY_ANALYZER_H
#define FREQUENCY_ANALYZER_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FREQUENCY_ANALYZER_HISTORY           3
#define FREQUENCY_ANALYZER_TRACE             32
#define FREQUENCY_ANALYZER_THRESHOLD_DEFAULT (-93)
#define FREQUENCY_ANALYZER_THRESHOLD_MIN     (-95)
#define FREQUENCY_ANALYZER_THRESHOLD_MAX     (-40)
#define FREQUENCY_ANALYZER_THRESHOLD_STEP    1
#define FREQUENCY_ANALYZER_FINE_STEP_HZ      20000U
typedef enum {
  FREQUENCY_ANALYZER_COMMON = 0,
  FREQUENCY_ANALYZER_315,
  FREQUENCY_ANALYZER_433,
  FREQUENCY_ANALYZER_868,
  FREQUENCY_ANALYZER_915,
  FREQUENCY_ANALYZER_ALL,
  FREQUENCY_ANALYZER_MODE_COUNT
} frequency_analyzer_mode_t;
typedef enum {
  FREQUENCY_ANALYZER_SEARCHING = 0,
  FREQUENCY_ANALYZER_REFINING,
  FREQUENCY_ANALYZER_FOUND,
  FREQUENCY_ANALYZER_PAUSED,
  FREQUENCY_ANALYZER_ERROR,
  FREQUENCY_ANALYZER_STOPPED,
  FREQUENCY_ANALYZER_PHASE_COUNT
} frequency_analyzer_phase_t;
typedef struct {
  uint32_t frequency_hz;
  float rssi_dbm;
} frequency_analyzer_hit_t;
typedef struct {
  frequency_analyzer_mode_t mode;
  frequency_analyzer_phase_t phase;
  int16_t threshold_dbm;
  esp_err_t error;
  uint32_t scan_frequency_hz, frequency_hz, detections, sweeps;
  uint32_t sweep_us, refine_us, samples;
  float rssi_dbm, live_dbm;
  uint16_t progress, total;
  uint8_t trace_count;
  float trace[FREQUENCY_ANALYZER_TRACE];
  frequency_analyzer_hit_t history[FREQUENCY_ANALYZER_HISTORY];
} frequency_analyzer_snapshot_t;
// Worker retains exclusive radio ownership until checked IDLE cleanup succeeds.
esp_err_t frequency_analyzer_start(frequency_analyzer_mode_t mode, int16_t threshold_dbm);
void frequency_analyzer_stop(void);
bool frequency_analyzer_is_running(void);
void frequency_analyzer_get_snapshot(frequency_analyzer_snapshot_t *out_snapshot);
esp_err_t frequency_analyzer_configure(frequency_analyzer_mode_t mode, int16_t threshold_dbm);
void frequency_analyzer_pause(bool is_paused);
const char *frequency_analyzer_mode_name(frequency_analyzer_mode_t mode);
uint16_t frequency_analyzer_plan_count(frequency_analyzer_mode_t mode);
uint32_t frequency_analyzer_plan_frequency(frequency_analyzer_mode_t mode, uint16_t index);
#ifdef __cplusplus
}
#endif
#endif
