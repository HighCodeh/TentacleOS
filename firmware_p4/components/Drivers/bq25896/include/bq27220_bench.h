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

#ifndef BQ27220_BENCH_H
#define BQ27220_BENCH_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  uint16_t voltage_mv;
  int16_t current_ma;
  int16_t average_current_ma;
  uint16_t remaining_mah;
  uint16_t full_mah;
  uint16_t design_mah;
  uint16_t soc_pct;
  uint16_t internal_temperature_dk;
  uint16_t battery_status;
  uint16_t operation_status;
} bq27220_sample_t;

/* Raw gauge estimates, not qualified SoC. Output is unchanged on failure. */
esp_err_t bq27220_read_sample(bq27220_sample_t *out_sample);
esp_err_t bq27220_bench(bool apply_capacity);
#ifdef __cplusplus
}
#endif
#endif
