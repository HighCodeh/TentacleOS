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

/**
 * @file qmi8658a.h
 * @brief Minimal QST QMI8658A IMU driver (SPI).
 *
 * Provides chip-id sanity check plus configuration of the accel and gyro
 * blocks with fixed-but-reasonable defaults (+/-4g / +/-512dps @ 250 Hz) and
 * converted reads in g / dps.
 */

#ifndef QMI8658A_H
#define QMI8658A_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief Expected WHO_AM_I value identifying the QMI8658A. */
#define QMI8658A_CHIP_ID_EXPECTED 0x05

/** @brief Three-axis vector of float samples (g or dps). */
typedef struct {
  float x;
  float y;
  float z;
} qmi8658a_vec3_t;

/** @brief Initialize the QMI8658A SPI driver. */
esp_err_t qmi8658a_init(void);

/** @brief Read the WHO_AM_I register (0x00). 0x05 = QMI8658A. */
esp_err_t qmi8658a_read_chip_id(uint8_t *out_id);

/**
 * @brief Configure the IMU with the driver's default profile.
 *
 * CTRL1: auto-increment serial reads, little-endian.
 * CTRL2: accel +/-4g, ODR 250 Hz.
 * CTRL3: gyro +/-512 dps, ODR 250 Hz.
 * CTRL7: accel + gyro enabled.
 *
 * Must be called once after qmi8658a_init() before read_accel/read_gyro.
 */
esp_err_t qmi8658a_configure(void);

/** @brief Read accelerometer in g (range +/-4g). */
esp_err_t qmi8658a_read_accel(qmi8658a_vec3_t *out_g);

/** @brief Read gyroscope in degrees per second (range +/-512 dps). */
esp_err_t qmi8658a_read_gyro(qmi8658a_vec3_t *out_dps);

/** @brief True if the accel/gyro have produced a fresh sample since the last read. */
esp_err_t qmi8658a_data_ready(bool *out_accel, bool *out_gyro);

#ifdef __cplusplus
}
#endif

#endif // QMI8658A_H
