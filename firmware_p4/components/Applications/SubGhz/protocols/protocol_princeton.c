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

static const char *TAG = "PROTOCOL_PRINCETON";

// Princeton / PT2262 Protocol Implementation (Alternate)

#define PRINCETON_SHORT_US      350
#define PRINCETON_LONG_US       1050
#define PRINCETON_TOLERANCE_PCT 60
#define PRINCETON_MIN_BITS      12
#define PRINCETON_MAX_BITS      24
#define PRINCETON_BTN_MASK      0x0F
#define PRINCETON_BTN_SHIFT     4
#define PRINCETON_STEP_SIZE     2

static bool
protocol_princeton_decode(const int32_t *raw_data, size_t count, subghz_data_t *out_data) {
  if (count < PRINCETON_MAX_BITS) {
    return false;
  }

  for (size_t start_idx = 0; start_idx < count - PRINCETON_MAX_BITS; start_idx++) {
    uint32_t decoded_data = 0;
    int bits_found = 0;
    size_t k = start_idx;
    bool is_fail = false;

    while (k < count - 1 && bits_found < PRINCETON_MAX_BITS) {
      int32_t pulse = raw_data[k];
      int32_t gap = raw_data[k + 1];

      if (pulse <= 0 || gap >= 0) {
        is_fail = true;
        break;
      }

      if (subghz_check_pulse(pulse, PRINCETON_SHORT_US, PRINCETON_TOLERANCE_PCT) &&
          subghz_check_pulse(gap, PRINCETON_LONG_US, PRINCETON_TOLERANCE_PCT)) {
        decoded_data = (decoded_data << 1) | 0;
        bits_found++;
      } else if (subghz_check_pulse(pulse, PRINCETON_LONG_US, PRINCETON_TOLERANCE_PCT) &&
                 subghz_check_pulse(gap, PRINCETON_SHORT_US, PRINCETON_TOLERANCE_PCT)) {
        decoded_data = (decoded_data << 1) | 1;
        bits_found++;
      } else {
        is_fail = true;
        break;
      }
      k += PRINCETON_STEP_SIZE;
    }

    if (is_fail == false && bits_found >= PRINCETON_MIN_BITS) {
      out_data->protocol_name = "Princeton (Alt)";
      out_data->bit_count = bits_found;
      out_data->raw_value = decoded_data;
      out_data->serial = decoded_data >> PRINCETON_BTN_SHIFT;
      out_data->btn = decoded_data & PRINCETON_BTN_MASK;
      ESP_LOGD(TAG, "Decoded %s", out_data->protocol_name);
      return true;
    }
  }

  return false;
}

static size_t
protocol_princeton_encode(const subghz_data_t *data, int32_t *pulses, size_t max_count) {
  return subghz_pwm_encode(
      data->raw_value, data->bit_count, PRINCETON_SHORT_US, PRINCETON_LONG_US, pulses, max_count);
}

subghz_protocol_t protocol_princeton = {
    .name = "Princeton", .decode = protocol_princeton_decode, .encode = protocol_princeton_encode};
