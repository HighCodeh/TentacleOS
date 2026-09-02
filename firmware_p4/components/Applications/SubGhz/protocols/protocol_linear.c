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

#include "subghz_protocol_decoder.h"

#include "esp_log.h"

#include "subghz_protocol_utils.h"

static const char *TAG = "PROTOCOL_LINEAR";

#define LINEAR_SHORT_US       500
#define LINEAR_LONG_US        1500
#define LINEAR_TOLERANCE_PCT  40
#define LINEAR_BIT_COUNT      10
#define LINEAR_MIN_RAW_COUNT  10
#define LINEAR_MIN_VALID_BITS 8
#define LINEAR_STEP_SIZE      2
#define LINEAR_BIT_INVALID    (-1)

static bool protocol_linear_decode(const int32_t *raw_data, size_t count, subghz_data_t *out_data) {
  if (count < LINEAR_MIN_RAW_COUNT) {
    return false;
  }

  size_t start = (raw_data[0] < 0) ? 1 : 0;
  uint32_t decoded_data = 0;
  int bits_found = 0;

  for (size_t i = start; i < count - 1; i += LINEAR_STEP_SIZE) {
    int32_t pulse = raw_data[i];
    int32_t gap = raw_data[i + 1];

    if (pulse < 0) {
      return false;
    }

    int bit = LINEAR_BIT_INVALID;

    if (subghz_check_pulse(pulse, LINEAR_SHORT_US, LINEAR_TOLERANCE_PCT) &&
        subghz_check_pulse(gap, LINEAR_LONG_US, LINEAR_TOLERANCE_PCT)) {
      bit = 0;
    } else if (subghz_check_pulse(pulse, LINEAR_LONG_US, LINEAR_TOLERANCE_PCT) &&
               subghz_check_pulse(gap, LINEAR_SHORT_US, LINEAR_TOLERANCE_PCT)) {
      bit = 1;
    } else {
      if (bits_found >= LINEAR_MIN_VALID_BITS) {
        break;
      }
      decoded_data = 0;
      bits_found = 0;
      continue;
    }

    if (bit != LINEAR_BIT_INVALID) {
      decoded_data = (decoded_data << 1) | bit;
      bits_found++;

      if (bits_found == LINEAR_BIT_COUNT) {
        out_data->protocol_name = "Linear";
        out_data->bit_count = bits_found;
        out_data->raw_value = decoded_data;
        out_data->serial = decoded_data;
        out_data->btn = 0;
        ESP_LOGD(TAG, "Decoded %s", out_data->protocol_name);
        return true;
      }
    }
  }
  return false;
}

static size_t
protocol_linear_encode(const subghz_data_t *data, int32_t *pulses, size_t max_count) {
  return subghz_pwm_encode(
      data->raw_value, data->bit_count, LINEAR_SHORT_US, LINEAR_LONG_US, pulses, max_count);
}

subghz_protocol_t protocol_linear = {
    .name = "Linear", .decode = protocol_linear_decode, .encode = protocol_linear_encode};
