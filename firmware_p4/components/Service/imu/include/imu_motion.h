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

#ifndef IMU_MOTION_H
#define IMU_MOTION_H
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool running, calibrated, zeroing;
  uint32_t samples, errors, gaps, zero_count;
  int64_t sample_us;
  esp_err_t error;
  float matrix[9], angles[3], accel[3], gyro[3], bias[3], temperature, rate_hz;
} imu_motion_snapshot_t;
esp_err_t imu_motion_start(void);
esp_err_t imu_motion_stop(void);
void imu_motion_snapshot(imu_motion_snapshot_t *out);
void imu_motion_request(bool calibrate);
void imu_motion_log(void);

#ifdef __cplusplus
}
#endif

#endif
