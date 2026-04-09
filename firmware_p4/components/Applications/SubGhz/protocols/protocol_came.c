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

static const char *TAG = "PROTOCOL_CAME";

// CAME 12bit / 24bit Protocol Implementation

#define CAME_SHORT_US        320
#define CAME_LONG_US         640
#define CAME_TOLERANCE_PCT   60
#define CAME_MIN_BITS        12
#define CAME_MAX_BITS        24
#define CAME_MIN_RAW_COUNT   24
#define CAME_STEP_SIZE       2
#define CAME_STOP_GAP_MULT   20
#define CAME_ENCODE_OVERHEAD 1

static bool protocol_came_decode(const int32_t *raw_data, size_t count, subghz_data_t *out_data) {
  if (count < CAME_MIN_RAW_COUNT) {
    return false;
  }

  for (size_t start_idx = 0; start_idx < count - CAME_MIN_RAW_COUNT; start_idx++) {
    uint32_t decoded_data = 0;
    int bits_found = 0;
    size_t k = start_idx;

    while (k < count - 1 && bits_found < CAME_MAX_BITS) {
      int32_t pulse = raw_data[k];
      int32_t gap = raw_data[k + 1];

      if (pulse <= 0 || gap >= 0) {
        break;
      }

      if (subghz_check_pulse(pulse, CAME_SHORT_US, CAME_TOLERANCE_PCT) &&
          subghz_check_pulse(gap, CAME_LONG_US, CAME_TOLERANCE_PCT)) {
        decoded_data = (decoded_data << 1) | 0;
        bits_found++;
      } else if (subghz_check_pulse(pulse, CAME_LONG_US, CAME_TOLERANCE_PCT) &&
                 subghz_check_pulse(gap, CAME_SHORT_US, CAME_TOLERANCE_PCT)) {
        decoded_data = (decoded_data << 1) | 1;
        bits_found++;
      } else {
        break;
      }
      k += CAME_STEP_SIZE;

      if (bits_found == CAME_MIN_BITS || bits_found == CAME_MAX_BITS) {
        if (k >= count - 1 ||
            (!subghz_check_pulse(raw_data[k], CAME_SHORT_US, CAME_TOLERANCE_PCT) &&
             !subghz_check_pulse(raw_data[k], CAME_LONG_US, CAME_TOLERANCE_PCT))) {
          out_data->protocol_name = (bits_found == CAME_MIN_BITS) ? "CAME 12bit" : "CAME 24bit";
          out_data->bit_count = bits_found;
          out_data->raw_value = decoded_data;
          out_data->serial = decoded_data;
          out_data->btn = 0;
          ESP_LOGD(TAG, "Decoded %s", out_data->protocol_name);
          return true;
        }
      }
    }
  }

  return false;
}

static size_t protocol_came_encode(const subghz_data_t *data, int32_t *pulses, size_t max_count) {
  if (data->bit_count != CAME_MIN_BITS && data->bit_count != CAME_MAX_BITS) {
    return 0;
  }
  if (max_count < (size_t)(data->bit_count * CAME_STEP_SIZE + CAME_ENCODE_OVERHEAD)) {
    return 0;
  }

  size_t idx = 0;

  for (int i = data->bit_count - 1; i >= 0; i--) {
    bool bit = (data->raw_value >> i) & 1;
    if (bit) {
      pulses[idx++] = CAME_LONG_US;
      pulses[idx++] = -CAME_SHORT_US;
    } else {
      pulses[idx++] = CAME_SHORT_US;
      pulses[idx++] = -CAME_LONG_US;
    }
  }

  pulses[idx++] = -CAME_SHORT_US * CAME_STOP_GAP_MULT;

  return idx;
}

subghz_protocol_t protocol_came = {
    .name = "CAME", .decode = protocol_came_decode, .encode = protocol_came_encode};
