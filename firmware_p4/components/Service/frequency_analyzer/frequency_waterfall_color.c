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

#include "frequency_waterfall_color.h"

#include <math.h>

#include "esp_log.h"

#include "frequency_waterfall.h"

#define COLOR_LEVEL_MAX           255U
#define COLOR_MIN_RANGE_DB        24.0f
#define COLOR_NOISE_MARGIN_DB     3.0f
#define COLOR_PEAK_MARGIN_DB      1.0f
#define COLOR_NOISE_PERCENTILE    20U
#define COLOR_PERCENT_BASE        100U
#define COLOR_SMOOTHING           0.15f
#define COLOR_ATTACK              0.5f
#define COLOR_INITIAL_FLOOR_DBM   (-120.0f)
#define COLOR_INITIAL_CEILING_DBM (-96.0f)

static const char *TAG = "WATERFALL_COLOR";

void frequency_waterfall_color_reset(frequency_waterfall_color_t *scale) {
  if (scale != NULL)
    *scale = (frequency_waterfall_color_t){.floor_dbm = COLOR_INITIAL_FLOOR_DBM,
                                           .ceiling_dbm = COLOR_INITIAL_CEILING_DBM};
}

esp_err_t frequency_waterfall_color_update(frequency_waterfall_color_t *scale,
                                           const float *samples,
                                           size_t count) {
  if (scale == NULL || samples == NULL || count == 0 || count > FREQUENCY_WATERFALL_BINS) {
    ESP_LOGE(TAG, "Invalid color scale input");
    return ESP_ERR_INVALID_ARG;
  }
  float sorted[FREQUENCY_WATERFALL_BINS];
  for (size_t i = 0; i < count; ++i) {
    if (!isfinite(samples[i])) {
      ESP_LOGE(TAG, "Non-finite RSSI in color scale");
      return ESP_ERR_INVALID_ARG;
    }
    size_t j = i;
    while (j > 0 && sorted[j - 1] > samples[i]) {
      sorted[j] = sorted[j - 1];
      --j;
    }
    sorted[j] = samples[i];
  }
  float floor =
      sorted[(count - 1) * COLOR_NOISE_PERCENTILE / COLOR_PERCENT_BASE] - COLOR_NOISE_MARGIN_DB;
  float ceiling = fmaxf(sorted[count - 1] + COLOR_PEAK_MARGIN_DB, floor + COLOR_MIN_RANGE_DB);
  if (!scale->is_ready) {
    scale->floor_dbm = floor;
    scale->ceiling_dbm = ceiling;
    scale->is_ready = true;
  } else {
    scale->floor_dbm += COLOR_SMOOTHING * (floor - scale->floor_dbm);
    float weight = ceiling > scale->ceiling_dbm ? COLOR_ATTACK : COLOR_SMOOTHING;
    scale->ceiling_dbm += weight * (ceiling - scale->ceiling_dbm);
    scale->ceiling_dbm = fmaxf(scale->ceiling_dbm, scale->floor_dbm + COLOR_MIN_RANGE_DB);
  }
  return ESP_OK;
}

uint8_t frequency_waterfall_color_level(const frequency_waterfall_color_t *scale, float dbm) {
  if (scale == NULL || !isfinite(dbm) || scale->ceiling_dbm <= scale->floor_dbm)
    return 0;
  float level =
      (dbm - scale->floor_dbm) * COLOR_LEVEL_MAX / (scale->ceiling_dbm - scale->floor_dbm);
  return level <= 0 ? 0 : level >= COLOR_LEVEL_MAX ? COLOR_LEVEL_MAX : (uint8_t)level;
}
