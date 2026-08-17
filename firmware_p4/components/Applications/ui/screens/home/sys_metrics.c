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

#include "sys_metrics.h"

#include "driver/temperature_sensor.h"

#define SM_TEMP_MIN_C (-10)
#define SM_TEMP_MAX_C 80

static temperature_sensor_handle_t s_tsens = NULL;
static bool s_tsens_failed = false;

bool sys_metrics_die_temp_c(float *out_celsius) {
  if (out_celsius == NULL || s_tsens_failed) {
    return false;
  }
  if (s_tsens == NULL) {
    temperature_sensor_config_t cfg =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(SM_TEMP_MIN_C, SM_TEMP_MAX_C);
    if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK) {
      s_tsens = NULL;
      s_tsens_failed = true;
      return false;
    }
    if (temperature_sensor_enable(s_tsens) != ESP_OK) {
      temperature_sensor_uninstall(s_tsens);
      s_tsens = NULL;
      s_tsens_failed = true;
      return false;
    }
  }
  float celsius = 0.0f;
  if (temperature_sensor_get_celsius(s_tsens, &celsius) != ESP_OK) {
    return false;
  }
  *out_celsius = celsius;
  return true;
}
