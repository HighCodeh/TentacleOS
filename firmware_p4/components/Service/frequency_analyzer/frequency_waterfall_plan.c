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

#include "frequency_waterfall.h"
#include "cc1101_rf_path.h"

static const float BANDWIDTHS[] = {58.035714f, 101.5625f, 203.125f, 270.833333f, 406.25f, 650.0f};
uint8_t frequency_waterfall_bandwidth_count(void) {
  return sizeof(BANDWIDTHS) / sizeof(BANDWIDTHS[0]);
}
float frequency_waterfall_bandwidth(uint8_t index) {
  return index < frequency_waterfall_bandwidth_count() ? BANDWIDTHS[index] : 0;
}
uint32_t frequency_waterfall_bin_hz(const frequency_waterfall_config_t *config, uint16_t bin) {
  if (config == NULL || bin >= FREQUENCY_WATERFALL_BINS)
    return 0;
  return config->center_hz - config->span_hz / 2 +
         (uint64_t)config->span_hz * bin / (FREQUENCY_WATERFALL_BINS - 1);
}
esp_err_t frequency_waterfall_validate(const frequency_waterfall_config_t *config) {
  if (config == NULL || config->bandwidth >= frequency_waterfall_bandwidth_count() ||
      config->span_hz < FREQUENCY_WATERFALL_MIN_SPAN ||
      config->span_hz > FREQUENCY_WATERFALL_MAX_SPAN || config->center_hz < config->span_hz / 2)
    return ESP_ERR_INVALID_ARG;
  uint32_t low = frequency_waterfall_bin_hz(config, 0);
  uint32_t high = frequency_waterfall_bin_hz(config, FREQUENCY_WATERFALL_BINS - 1);
  if (high < low)
    return ESP_ERR_INVALID_ARG;
  if ((low >= CC1101_BAND_315_MIN_HZ && high <= CC1101_BAND_315_MAX_HZ) ||
      (low >= CC1101_BAND_433_MIN_HZ && high <= CC1101_BAND_433_MAX_HZ) ||
      (low >= CC1101_BAND_HIGH_MIN_HZ && high <= CC1101_BAND_HIGH_MAX_HZ))
    return ESP_OK;
  return ESP_ERR_INVALID_ARG;
}

esp_err_t frequency_waterfall_fit_center(frequency_waterfall_config_t *config,
                                         uint32_t frequency_hz) {
  if (config == NULL)
    return ESP_ERR_INVALID_ARG;
  static const uint32_t bands[][2] = {{CC1101_BAND_315_MIN_HZ, CC1101_BAND_315_MAX_HZ},
                                      {CC1101_BAND_433_MIN_HZ, CC1101_BAND_433_MAX_HZ},
                                      {CC1101_BAND_HIGH_MIN_HZ, CC1101_BAND_HIGH_MAX_HZ}};
  for (unsigned i = 0; i < sizeof(bands) / sizeof(bands[0]); ++i) {
    if (frequency_hz < bands[i][0] || frequency_hz > bands[i][1])
      continue;
    frequency_waterfall_config_t candidate = *config;
    if (candidate.span_hz < FREQUENCY_WATERFALL_MIN_SPAN ||
        candidate.span_hz > FREQUENCY_WATERFALL_MAX_SPAN)
      return ESP_ERR_INVALID_ARG;
    uint32_t low = bands[i][0] + candidate.span_hz / 2;
    uint32_t high = bands[i][1] - (candidate.span_hz - candidate.span_hz / 2);
    candidate.center_hz = frequency_hz < low ? low : frequency_hz > high ? high : frequency_hz;
    esp_err_t error = frequency_waterfall_validate(&candidate);
    if (error == ESP_OK)
      *config = candidate;
    return error;
  }
  return ESP_ERR_INVALID_ARG;
}
