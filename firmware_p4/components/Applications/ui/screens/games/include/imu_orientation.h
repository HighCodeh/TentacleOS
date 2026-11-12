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

#ifndef IMU_ORIENTATION_H
#define IMU_ORIENTATION_H
#include <stdbool.h>
#include <stdint.h>
#define IMU_ZERO_SAMPLES 160

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float q[4], reference[4], gravity[3];
  bool initialized;
} imu_orientation_t;
typedef struct {
  unsigned count;
  float mean[6], m2[6];
} imu_zero_t;
void imu_orientation_reset(imu_orientation_t *state);
bool imu_orientation_update(imu_orientation_t *state,
                            const float accel[3],
                            const float gyro_dps[3],
                            float dt);
void imu_orientation_recenter(imu_orientation_t *state);
void imu_orientation_matrix(const imu_orientation_t *state, float matrix[9], float angles[3]);
void imu_zero_reset(imu_zero_t *zero);
/* 0: collecting, 1: accepted, -1: moving/invalid. Bias unchanged on rejection. */
int imu_zero_sample(imu_zero_t *zero, const float accel[3], const float gyro[3], float bias[3]);

#ifdef __cplusplus
}
#endif

#endif
