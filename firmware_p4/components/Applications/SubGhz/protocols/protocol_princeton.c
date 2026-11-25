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
#define PRINCETON_MIN_BITS      24
#define PRINCETON_MAX_BITS      24
#define PRINCETON_BTN_MASK      0x0F
#define PRINCETON_BTN_SHIFT     4
#define PRINCETON_STEP_SIZE     2
#define PRINCETON_STOP_FACTOR   30U
#define PRINCETON_GUARD_MIN     15U
#define PRINCETON_GUARD_MAX     72U
#define PRINCETON_LONG_FACTOR   3U
#define PRINCETON_BIT_FACTOR    4U
#define PRINCETON_MIN_GAP_US    (PRINCETON_LONG_US * 2)

static bool
protocol_princeton_decode(const int32_t *raw_data, size_t count, subghz_data_t *out_data) {
  if (raw_data == NULL || out_data == NULL ||
      count < PRINCETON_MAX_BITS * PRINCETON_STEP_SIZE + 1) {
    return false;
  }

  for (size_t start_idx = 0; start_idx < count - PRINCETON_MAX_BITS; start_idx++) {
    if (start_idx != 0 && raw_data[start_idx - 1] > -PRINCETON_MIN_GAP_US)
      continue;
    uint32_t decoded_data = 0;
    int bits_found = 0;
    size_t k = start_idx;
    bool is_fail = false;
    uint64_t duration_sum = 0;

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
      duration_sum += (uint32_t)pulse + (uint64_t)(-(int64_t)gap);
      k += PRINCETON_STEP_SIZE;
    }

    bool has_stop = k < count &&
                    subghz_check_pulse(raw_data[k], PRINCETON_SHORT_US, PRINCETON_TOLERANCE_PCT) &&
                    raw_data[k] > 0 && (k + 1 == count || raw_data[k + 1] <= -PRINCETON_MIN_GAP_US);
    if (is_fail == false && bits_found == PRINCETON_MIN_BITS && has_stop) {
      out_data->protocol_name = "Princeton";
      out_data->bit_count = bits_found;
      out_data->raw_value = decoded_data;
      out_data->serial = decoded_data >> PRINCETON_BTN_SHIFT;
      out_data->btn = decoded_data & PRINCETON_BTN_MASK;
      uint32_t units = PRINCETON_MAX_BITS * PRINCETON_BIT_FACTOR + 1U;
      out_data->te_us = (uint32_t)((duration_sum + raw_data[k] + units / 2U) / units);
      out_data->guard_time = PRINCETON_STOP_FACTOR;
      if (k + 1 < count) {
        uint32_t guard =
            (uint32_t)((-(int64_t)raw_data[k + 1] + out_data->te_us / 2U) / out_data->te_us);
        if (guard >= PRINCETON_GUARD_MIN && guard <= PRINCETON_GUARD_MAX)
          out_data->guard_time = guard;
      }
      ESP_LOGD(TAG, "Decoded %s", out_data->protocol_name);
      return true;
    }
  }

  return false;
}

static size_t
protocol_princeton_encode(const subghz_data_t *data, int32_t *pulses, size_t max_count) {
  if (data == NULL || pulses == NULL || data->bit_count != PRINCETON_MAX_BITS ||
      max_count < PRINCETON_MAX_BITS * PRINCETON_STEP_SIZE + PRINCETON_STEP_SIZE)
    return 0;
  uint32_t te = data->te_us == 0 ? PRINCETON_SHORT_US : data->te_us;
  uint32_t guard = data->guard_time;
  if (guard < PRINCETON_GUARD_MIN || guard > PRINCETON_GUARD_MAX)
    guard = PRINCETON_STOP_FACTOR;
  if (te > INT32_MAX / PRINCETON_GUARD_MAX)
    return 0;
  size_t count = subghz_pwm_encode(
      data->raw_value, data->bit_count, te, te * PRINCETON_LONG_FACTOR, pulses, max_count);
  /* Flipper Princeton sends a high stop bit before the low guard interval. */
  pulses[count - 1] = (int32_t)te;
  pulses[count++] = -(int32_t)(te * guard);
  return count;
}

subghz_protocol_t protocol_princeton = {
    .name = "Princeton", .decode = protocol_princeton_decode, .encode = protocol_princeton_encode};
