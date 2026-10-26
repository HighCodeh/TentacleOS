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

#ifndef QMI8658A_H
#define QMI8658A_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
  float temperature_c;
  float accel_g[3];
  float gyro_dps[3];
  uint8_t status;
} qmi8658a_sample_t;

/** Register the SPI device, verify WHO_AM_I=0x05 and configure 6-DoF. */
esp_err_t qmi8658a_init(void);

/** Enable accelerometer and gyroscope. */
esp_err_t qmi8658a_start(void);

/** Disable accelerometer and gyroscope without removing the SPI device. */
esp_err_t qmi8658a_stop(void);

/**
 * Select the shared accel/gyro ODR register value.
 * Valid values are 5 (224 Hz), 6 (112 Hz), 7 (56 Hz), and 8 (28 Hz).
 */
esp_err_t qmi8658a_set_odr(uint8_t odr_bits);

/** Read temperature, acceleration and angular rate from one burst. */
esp_err_t qmi8658a_read(qmi8658a_sample_t *out);

bool qmi8658a_is_ready(void);
uint8_t qmi8658a_revision(void);

#ifdef __cplusplus
}
#endif

#endif // QMI8658A_H
