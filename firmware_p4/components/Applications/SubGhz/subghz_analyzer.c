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

#include "subghz_analyzer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"

static const char* TAG = "SUBGHZ_ANALYZER";

// Histogram resolution
#define HIST_BIN_SIZE 50
#define HIST_MAX_VAL  5000
#define HIST_BINS     (HIST_MAX_VAL / HIST_BIN_SIZE)

bool subghz_analyzer_process(const int32_t* pulses, size_t count, subghz_analyzer_result_t* out_result) {
  if (count < 10 || !out_result) return false;

  uint32_t min_p = 0xFFFFFFFF;
  uint32_t max_p = 0;
  uint32_t bins[HIST_BINS] = {0};

  // 1. Build Histogram and find Min/Max
  for (size_t i = 0; i < count; i++) {
    uint32_t duration = abs(pulses[i]);
    if (duration < 50) continue; // Noise filter

    if (duration < min_p) min_p = duration;
    if (duration > max_p) max_p = duration;

    size_t bin_idx = duration / HIST_BIN_SIZE;
    if (bin_idx < HIST_BINS) {
      bins[bin_idx]++;
    }
  }

  // 2. Find the first significant peak (likely the TE)
  int first_peak_bin = -1;

  for (int i = 1; i < HIST_BINS; i++) {
    // Simple peak detection: higher than neighbors and threshold
    if (bins[i] > 2 && bins[i] >= bins[i-1] && bins[i] >= bins[i+1]) {
      first_peak_bin = i;
      break; 
    }
  }

  if (first_peak_bin != -1) {
    out_result->estimated_te = first_peak_bin * HIST_BIN_SIZE;
  } else {
    out_result->estimated_te = min_p;
  }

  // 3. Modulation Heuristic
  // Manchester usually has only 2 dominant durations (T and 2T)
  // PWM usually has 3 (Sync, Short, Long)
  int distinct_peaks = 0;
  for (int i = 1; i < HIST_BINS - 1; i++) {
    if (bins[i] > (uint32_t)(count / 10) && bins[i] >= bins[i-1] && bins[i] >= bins[i+1]) {
      distinct_peaks++;
    }
  }

  if (distinct_peaks == 2) {
    out_result->modulation_hint = "Manchester/Biphase";
  } else if (distinct_peaks >= 3) {
    out_result->modulation_hint = "PWM/Tri-state";
  } else {
    out_result->modulation_hint = "Unknown/Custom";
  }

  out_result->pulse_min = min_p;
  out_result->pulse_max = max_p;
  out_result->pulse_count = count;

  // 4. Bitstream Recovery (Edge-to-Edge)
  // We use the estimated TE to slice the signal
  if (out_result->estimated_te > 50) {
    size_t bit_idx = 0;
    uint32_t te = out_result->estimated_te;

    for (size_t i = 0; i < count && bit_idx < 1024; i++) {
      int32_t duration = abs(pulses[i]);
      bool level = (pulses[i] > 0);

      // Calculate how many TEs fit in this pulse/gap
      int num_te = (duration + (te / 2)) / te;
      if (num_te == 0) num_te = 1;

      for (int n = 0; n < num_te && bit_idx < 1024; n++) {
        size_t byte_pos = bit_idx / 8;
        size_t bit_pos = 7 - (bit_idx % 8);

        if (level) {
          out_result->bitstream[byte_pos] |= (1 << bit_pos);
        } else {
          out_result->bitstream[byte_pos] &= ~(1 << bit_pos);
        }
        bit_idx++;
      }
    }
    out_result->bitstream_len = bit_idx;
  }

  ESP_LOGI(TAG, "Analysis Complete: TE ~%lu us, Peaks: %d, Hint: %s, Bits: %d", 
           (unsigned long)out_result->estimated_te, distinct_peaks, out_result->modulation_hint, (int)out_result->bitstream_len);

  return true;
}
