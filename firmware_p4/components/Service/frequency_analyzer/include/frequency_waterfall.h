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

#ifndef FREQUENCY_WATERFALL_H
#define FREQUENCY_WATERFALL_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FREQUENCY_WATERFALL_BINS     40
#define FREQUENCY_WATERFALL_MIN_SPAN 100000U
#define FREQUENCY_WATERFALL_MAX_SPAN 20000000U
typedef struct {
  uint32_t center_hz;
  uint32_t span_hz;
  uint8_t bandwidth;
} frequency_waterfall_config_t;
typedef struct {
  frequency_waterfall_config_t config;
  uint32_t sequence;
  uint32_t duration_us;
  uint16_t progress;
  bool is_paused;
  esp_err_t error;
  float dbm[FREQUENCY_WATERFALL_BINS];
} frequency_waterfall_snapshot_t;
esp_err_t frequency_waterfall_validate(const frequency_waterfall_config_t *config);
esp_err_t frequency_waterfall_fit_center(frequency_waterfall_config_t *config,
                                         uint32_t frequency_hz);
uint32_t frequency_waterfall_bin_hz(const frequency_waterfall_config_t *config, uint16_t bin);
float frequency_waterfall_bandwidth(uint8_t index);
uint8_t frequency_waterfall_bandwidth_count(void);
esp_err_t frequency_waterfall_start(const frequency_waterfall_config_t *config);
void frequency_waterfall_stop(void);
bool frequency_waterfall_is_running(void);
void frequency_waterfall_pause(bool is_paused);
void frequency_waterfall_get_snapshot(frequency_waterfall_snapshot_t *out_snapshot);
#ifdef __cplusplus
}
#endif
#endif
