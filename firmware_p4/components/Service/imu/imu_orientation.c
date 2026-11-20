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

#include "imu_orientation.h"
#include <math.h>
#include <string.h>
#define DEG_TO_RAD          0.017453292519943295f
#define RAD_TO_DEG          57.29577951308232f
#define MAX_DT              0.050f
#define GRAVITY_KP          1.8f
#define ACCEL_MIN           0.90f
#define ACCEL_MAX           1.10f
#define ZERO_ACCEL_VARIANCE 0.0004f
#define ZERO_GYRO_VARIANCE  0.0625f
#define ZERO_MAX_GYRO_DPS   10.0f

static void multiply(const float a[4], const float b[4], float out[4]) {
  float result[4] = {a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
                     a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
                     a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
                     a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0]};
  memcpy(out, result, sizeof(result));
}
static void rotation(const float q[4], float m[9]) {
  float w = q[0], x = q[1], y = q[2], z = q[3];
  m[0] = 1 - 2 * (y * y + z * z);
  m[1] = 2 * (x * y - w * z);
  m[2] = 2 * (x * z + w * y);
  m[3] = 2 * (x * y + w * z);
  m[4] = 1 - 2 * (x * x + z * z);
  m[5] = 2 * (y * z - w * x);
  m[6] = 2 * (x * z - w * y);
  m[7] = 2 * (y * z + w * x);
  m[8] = 1 - 2 * (x * x + y * y);
}
void imu_orientation_reset(imu_orientation_t *s) {
  memset(s, 0, sizeof(*s));
  s->q[0] = s->reference[0] = 1;
}
bool imu_orientation_update(imu_orientation_t *s, const float a[3], const float g[3], float dt) {
  if (!isfinite(dt) || dt <= 0 || dt > MAX_DT)
    return false;
  float norm2 = 0;
  for (int i = 0; i < 3; ++i) {
    if (!isfinite(a[i]) || !isfinite(g[i]) || fabsf(g[i]) > 600)
      return false;
    norm2 += a[i] * a[i];
  }
  if (!s->initialized) {
    if (norm2 < ACCEL_MIN * ACCEL_MIN || norm2 > ACCEL_MAX * ACCEL_MAX)
      return false;
    for (int i = 0; i < 3; ++i)
      s->gravity[i] = a[i] / sqrtf(norm2);
    s->initialized = true;
    return true;
  }
  float omega[4] = {0, g[0] * DEG_TO_RAD, g[1] * DEG_TO_RAD, g[2] * DEG_TO_RAD};
  /* Complementary gravity correction. No magnetic heading is invented.
     q maps body to the initial frame. Its inverse predicts gravity in body. */
  if (norm2 >= ACCEL_MIN * ACCEL_MIN && norm2 <= ACCEL_MAX * ACCEL_MAX) {
    float m[9], measured[3], predicted[3];
    rotation(s->q, m);
    for (int i = 0; i < 3; ++i) {
      measured[i] = a[i] / sqrtf(norm2);
      predicted[i] = m[i] * s->gravity[0] + m[3 + i] * s->gravity[1] + m[6 + i] * s->gravity[2];
    }
    for (int i = 0; i < 3; ++i) {
      int j = (i + 1) % 3, k = (i + 2) % 3;
      omega[i + 1] += GRAVITY_KP * (measured[j] * predicted[k] - measured[k] * predicted[j]);
    }
  }
  float derivative[4], length = 0;
  multiply(s->q, omega, derivative);
  for (int i = 0; i < 4; ++i) {
    s->q[i] += derivative[i] * dt * 0.5f;
    length += s->q[i] * s->q[i];
  }
  length = sqrtf(length);
  for (int i = 0; i < 4; ++i)
    s->q[i] /= length;
  return true;
}
void imu_orientation_recenter(imu_orientation_t *s) {
  memcpy(s->reference, s->q, sizeof(s->q));
}
void imu_orientation_matrix(const imu_orientation_t *s, float m[9], float angles[3]) {
  float inverse[4] = {s->reference[0], -s->reference[1], -s->reference[2], -s->reference[3]},
        relative[4];
  multiply(inverse, s->q, relative);
  rotation(relative, m);
  angles[0] = atan2f(m[7], m[8]) * RAD_TO_DEG;
  angles[1] = asinf(fmaxf(-1, fminf(1, -m[6]))) * RAD_TO_DEG;
  angles[2] = atan2f(m[3], m[0]) * RAD_TO_DEG;
}
void imu_zero_reset(imu_zero_t *z) {
  memset(z, 0, sizeof(*z));
}
int imu_zero_sample(imu_zero_t *z, const float a[3], const float g[3], float bias[3]) {
  for (int i = 0; i < 3; ++i)
    if (!isfinite(a[i]) || !isfinite(g[i])) {
      imu_zero_reset(z);
      return -1;
    }
  ++z->count;
  for (int i = 0; i < 6; ++i) {
    float value = i < 3 ? a[i] : g[i - 3], delta = value - z->mean[i];
    z->mean[i] += delta / z->count;
    z->m2[i] += delta * (value - z->mean[i]);
  }
  if (z->count < IMU_ZERO_SAMPLES)
    return 0;
  bool stable = true;
  float norm2 = 0;
  for (int i = 0; i < 6; ++i) {
    if (z->m2[i] / (z->count - 1) > (i < 3 ? ZERO_ACCEL_VARIANCE : ZERO_GYRO_VARIANCE))
      stable = false;
    if (i < 3)
      norm2 += z->mean[i] * z->mean[i];
    else if (fabsf(z->mean[i]) > ZERO_MAX_GYRO_DPS)
      stable = false;
  }
  stable = stable && norm2 > ACCEL_MIN * ACCEL_MIN && norm2 < ACCEL_MAX * ACCEL_MAX;
  if (stable)
    memcpy(bias, z->mean + 3, 3 * sizeof(float));
  imu_zero_reset(z);
  return stable ? 1 : -1;
}
