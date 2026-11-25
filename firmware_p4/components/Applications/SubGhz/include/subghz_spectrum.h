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

#ifndef SUBGHZ_SPECTRUM_H
#define SUBGHZ_SPECTRUM_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "cc1101.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Number of frequency bins per spectrum sweep. */
#define SPECTRUM_SAMPLES 80

/**
 * @brief Single spectrum sweep result.
 */
typedef struct {
  uint32_t center_freq;               /**< @brief Center frequency in Hz. */
  uint32_t span_hz;                   /**< @brief Total span width in Hz. */
  uint32_t start_freq;                /**< @brief Start frequency in Hz. */
  uint32_t step_hz;                   /**< @brief Frequency step between bins in Hz. */
  float dbm_values[SPECTRUM_SAMPLES]; /**< @brief RSSI values in dBm per bin. */
  uint64_t timestamp;                 /**< @brief Capture timestamp in microseconds. */
} subghz_spectrum_line_t;

/**
 * @brief Start a continuous spectrum sweep.
 *
 * @param center_freq  Center frequency in Hz.
 * @param span_hz      Sweep span width in Hz.
 */
esp_err_t subghz_spectrum_start(uint32_t center_freq, uint32_t span_hz);

/* Copy a complete profile before starting; NULL preserves the legacy profile. */
esp_err_t subghz_spectrum_start_configured(uint32_t center_freq,
                                           uint32_t span_hz,
                                           const cc1101_radio_config_t *radio);

/** Stop the spectrum sweep. */
void subghz_spectrum_stop(void);

/** First worker failure, retained until the next accepted start. */
esp_err_t subghz_spectrum_get_error(void);

/**
 * @brief Retrieve the latest spectrum sweep line.
 *
 * @param out_line  Pointer to store the sweep result.
 * @return true if a new line was available, false otherwise.
 */
bool subghz_spectrum_get_line(subghz_spectrum_line_t *out_line);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_SPECTRUM_H
