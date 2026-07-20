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
 * @file drv2605l.h
 * @brief Minimal TI DRV2605L haptic driver (I2C @ 0x5A, EN on GPIO37).
 *
 * Uses the chip's internal ROM waveform library. RTP (real-time playback)
 * is exposed for variable-intensity feedback.
 */

#ifndef DRV2605L_H
#define DRV2605L_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

/** @brief I2C 7-bit slave address of the DRV2605L. */
#define DRV2605L_I2C_ADDR 0x5A

/**
 * @brief Initialise the DRV2605L driver.
 *
 * @return ESP_OK on success, otherwise an esp_err_t error code.
 */
esp_err_t drv2605l_init(void);

/**
 * @brief Play a single library waveform effect (1..123) once.
 *
 * @param effect Effect ID from the ROM library.
 */
esp_err_t drv2605l_play_effect(uint8_t effect);

/** @brief Stop any currently-playing waveform. */
esp_err_t drv2605l_stop(void);

/**
 * @brief Enter RTP mode and apply an 8-bit signed intensity.
 *
 * @param intensity 0..127 -> forward drive; useful range 0..127.
 */
esp_err_t drv2605l_set_rtp(uint8_t intensity);

/** @brief Last-read DEVICE_ID register (0 if not yet probed). */
uint8_t drv2605l_device_id(void);

/**
 * @brief Run the DRV2605L ERM auto-calibration (MODE=0x07) against the actuator
 *        and return to internal-trigger mode. Uses the rated/overdrive/feedback
 *        registers already programmed by drv2605l_init(). Blocks ~up to 1.5 s
 *        while polling GO. Returns ESP_OK if DIAG_RESULT passed, else ESP_FAIL.
 *        Opt-in (not run at boot) so it never regresses the working manual tune.
 */
esp_err_t drv2605l_autocal(void);

#ifdef __cplusplus
}
#endif

#endif // DRV2605L_H
