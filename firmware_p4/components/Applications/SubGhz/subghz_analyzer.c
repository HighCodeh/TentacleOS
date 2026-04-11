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

static const char *TAG = "SUBGHZ_ANALYZERR";

#define HIST_BIN_SIZE      50
#define HIST_MAX_VAL       5000
#define HIST_BINS          (HIST_MAX_VAL / HIST_BIN_SIZE)
#define NOISE_FLOOR_US     50
#define MIN_PULSE_COUNT    10
#define PEAK_THRESHOLD     2
#define MODULATION_DIVISOR 10
#define MAX_BITSTREAM_BITS 1024
#define MIN_TE_THRESHOLD   50

bool subghz_analyzer_process(const int32_t *pulses,
                             size_t count,
                             subghz_analyzer_result_t *out_result) {
  if (count < MIN_PULSE_COUNT || out_result == NULL) {
    return false;
  }

  uint32_t min_p = 0xFFFFFFFF;
  uint32_t max_p = 0;
  uint32_t bins[HIST_BINS] = {0};

  for (size_t i = 0; i < count; i++) {
    uint32_t duration = (uint32_t)abs(pulses[i]);
    if (duration < NOISE_FLOOR_US) {
      continue;
    }

    if (duration < min_p) {
      min_p = duration;
    }
    if (duration > max_p) {
      max_p = duration;
    }

    size_t bin_idx = duration / HIST_BIN_SIZE;
    if (bin_idx < HIST_BINS) {
      bins[bin_idx]++;
    }
  }

  int first_peak_bin = -1;
  for (int i = 1; i < HIST_BINS - 1; i++) {
    if (bins[i] > PEAK_THRESHOLD && bins[i] >= bins[i - 1] && bins[i] >= bins[i + 1]) {
      first_peak_bin = i;
      break;
    }
  }

  if (first_peak_bin != -1) {
    out_result->estimated_te = (uint32_t)(first_peak_bin * HIST_BIN_SIZE);
  } else {
    out_result->estimated_te = min_p;
  }

  int distinct_peaks = 0;
  for (int i = 1; i < HIST_BINS - 1; i++) {
    if (bins[i] > (uint32_t)(count / MODULATION_DIVISOR) && bins[i] >= bins[i - 1] &&
        bins[i] >= bins[i + 1]) {
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

  if (out_result->estimated_te > MIN_TE_THRESHOLD) {
    size_t bit_idx = 0;
    uint32_t te = out_result->estimated_te;

    for (size_t i = 0; i < count && bit_idx < MAX_BITSTREAM_BITS; i++) {
      int32_t duration = abs(pulses[i]);
      bool level = (pulses[i] > 0);

      int num_te = (duration + (int32_t)(te / 2)) / (int32_t)te;
      if (num_te == 0) {
        num_te = 1;
      }

      for (int n = 0; n < num_te && bit_idx < MAX_BITSTREAM_BITS; n++) {
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

  ESP_LOGI(TAG,
           "Analysis Complete: TE ~%lu us, Peaks: %d, Hint: %s, Bits: %d",
           (unsigned long)out_result->estimated_te,
           distinct_peaks,
           out_result->modulation_hint,
           (int)out_result->bitstream_len);

  return true;
}
