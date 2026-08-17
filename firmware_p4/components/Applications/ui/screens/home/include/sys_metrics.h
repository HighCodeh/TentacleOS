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

#ifndef SYS_METRICS_H
#define SYS_METRICS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Read the P4 die temperature in Celsius.
 *
 * The chip has a single temperature-sensor peripheral, so this owns one shared
 * handle installed on first use — any screen may call it without clashing over
 * the driver. Returns false (and leaves @p out_celsius untouched) if the sensor
 * is unavailable.
 *
 * @param[out] out_celsius  Receives the temperature on success.
 * @return true on a valid reading, false otherwise.
 */
bool sys_metrics_die_temp_c(float *out_celsius);

#ifdef __cplusplus
}
#endif

#endif // SYS_METRICS_H
