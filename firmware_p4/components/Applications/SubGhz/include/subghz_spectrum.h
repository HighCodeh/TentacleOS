// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SUBGHZ_SPECTRUM_H
#define SUBGHZ_SPECTRUM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Number of frequency bins per spectrum sweep. */
#define SPECTRUM_SAMPLES 80

/**
 * @brief Single spectrum sweep result.
 */
typedef struct {
  uint32_t center_freq;                /**< @brief Center frequency in Hz. */
  uint32_t span_hz;                    /**< @brief Total span width in Hz. */
  uint32_t start_freq;                 /**< @brief Start frequency in Hz. */
  uint32_t step_hz;                    /**< @brief Frequency step between bins in Hz. */
  float dbm_values[SPECTRUM_SAMPLES];  /**< @brief RSSI values in dBm per bin. */
  uint64_t timestamp;                  /**< @brief Capture timestamp in microseconds. */
} subghz_spectrum_line_t;

/**
 * @brief Start a continuous spectrum sweep.
 *
 * @param center_freq  Center frequency in Hz.
 * @param span_hz      Sweep span width in Hz.
 */
void subghz_spectrum_start(uint32_t center_freq, uint32_t span_hz);

/**
 * @brief Stop the spectrum sweep.
 */
void subghz_spectrum_stop(void);

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
