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

#ifndef FREQUENCY_WATERFALL_COLOR_H
#define FREQUENCY_WATERFALL_COLOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float floor_dbm;
  float ceiling_dbm;
  bool is_ready;
} frequency_waterfall_color_t;

void frequency_waterfall_color_reset(frequency_waterfall_color_t *scale);
esp_err_t frequency_waterfall_color_update(frequency_waterfall_color_t *scale,
                                           const float *samples,
                                           size_t count);
uint8_t frequency_waterfall_color_level(const frequency_waterfall_color_t *scale, float dbm);

#ifdef __cplusplus
}
#endif
#endif
