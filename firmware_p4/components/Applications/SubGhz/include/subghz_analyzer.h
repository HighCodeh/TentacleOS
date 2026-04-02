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

#ifndef SUBGHZ_ANALYZER_H
#define SUBGHZ_ANALYZER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of Sub-GHz signal analysis.
 */
typedef struct {
  uint32_t estimated_te;       /**< @brief Base time element in microseconds. */
  uint32_t pulse_min;          /**< @brief Minimum observed pulse width. */
  uint32_t pulse_max;          /**< @brief Maximum observed pulse width. */
  size_t pulse_count;          /**< @brief Total number of pulses analyzed. */
  const char *modulation_hint; /**< @brief Modulation guess: "PWM", "Manchester", or "Unknown". */
  uint8_t bitstream[128];      /**< @brief Buffer for expected bits. */
  size_t bitstream_len;        /**< @brief Number of recovered bits. */
} subghz_analyzer_result_t;

/**
 * @brief Analyze raw pulse data and extract signal characteristics.
 *
 * @param pulses      Array of signed pulse durations in microseconds.
 * @param count       Number of elements in the pulses array.
 * @param out_result  Pointer to store the analysis result.
 * @return true if analysis succeeded, false otherwise.
 */
bool subghz_analyzer_process(const int32_t *pulses, size_t count, subghz_analyzer_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_ANALYZER_H
