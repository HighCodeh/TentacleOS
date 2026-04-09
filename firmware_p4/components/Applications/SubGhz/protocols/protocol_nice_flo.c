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

#include "subghz_protocol_decoder.h"

#include "esp_log.h"

#include "subghz_protocol_utils.h"

static const char *TAG = "PROTOCOL_NICE_FLO";

// Nice Flo 12bit Protocol Implementation

#define NICE_FLO_SHORT_US      700
#define NICE_FLO_LONG_US       1400
#define NICE_FLO_TOLERANCE_PCT 60
#define NICE_FLO_BIT_COUNT     12
#define NICE_FLO_MIN_RAW_COUNT 24
#define NICE_FLO_STEP_SIZE     2
#define NICE_FLO_SERIAL_SHIFT  2
#define NICE_FLO_BTN_MASK      0x03

static bool
protocol_nice_flo_decode(const int32_t *raw_data, size_t count, subghz_data_t *out_data) {
  if (count < NICE_FLO_MIN_RAW_COUNT) {
    return false;
  }

  for (size_t start_idx = 0; start_idx < count - NICE_FLO_MIN_RAW_COUNT; start_idx++) {
    uint32_t decoded_data = 0;
    int bits_found = 0;
    size_t k = start_idx;
    bool is_fail = false;

    while (k < count - 1 && bits_found < NICE_FLO_BIT_COUNT) {
      int32_t pulse = raw_data[k];
      int32_t gap = raw_data[k + 1];

      if (pulse <= 0 || gap >= 0) {
        is_fail = true;
        break;
      }

      if (subghz_check_pulse(pulse, NICE_FLO_SHORT_US, NICE_FLO_TOLERANCE_PCT) &&
          subghz_check_pulse(gap, NICE_FLO_LONG_US, NICE_FLO_TOLERANCE_PCT)) {
        decoded_data = (decoded_data << 1) | 0;
        bits_found++;
      } else if (subghz_check_pulse(pulse, NICE_FLO_LONG_US, NICE_FLO_TOLERANCE_PCT) &&
                 subghz_check_pulse(gap, NICE_FLO_SHORT_US, NICE_FLO_TOLERANCE_PCT)) {
        decoded_data = (decoded_data << 1) | 1;
        bits_found++;
      } else {
        is_fail = true;
        break;
      }
      k += NICE_FLO_STEP_SIZE;
    }

    if (is_fail == false && bits_found == NICE_FLO_BIT_COUNT) {
      out_data->protocol_name = "Nice Flo 12bit";
      out_data->bit_count = NICE_FLO_BIT_COUNT;
      out_data->raw_value = decoded_data;
      out_data->serial = decoded_data >> NICE_FLO_SERIAL_SHIFT;
      out_data->btn = decoded_data & NICE_FLO_BTN_MASK;
      ESP_LOGD(TAG, "Decoded %s", out_data->protocol_name);
      return true;
    }
  }

  return false;
}

subghz_protocol_t protocol_nice_flo = {
    .name = "Nice Flo", .decode = protocol_nice_flo_decode, .encode = NULL};
