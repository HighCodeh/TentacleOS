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

#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"

#include "subghz_protocol_utils.h"

static const char *TAG = "PROTOCOL_RCSWITCH";

// RCSwitch Original Protocol Implementation
// Ported to TentacleOS for improved accuracy using derived timings.
//
// Note: The subghz_rcswitch_proto_t struct uses camelCase field names
// (pulseLength, syncFactor, etc.) to match the original RCSwitch
// reference implementation.

typedef struct {
  uint16_t pulseLength;
  struct {
    uint8_t high;
    uint8_t low;
  } syncFactor;
  struct {
    uint8_t high;
    uint8_t low;
  } zero;
  struct {
    uint8_t high;
    uint8_t low;
  } one;
  bool is_inverted_signal;
} subghz_rcswitch_proto_t;

static const subghz_rcswitch_proto_t RCSWITCH_PROTOCOLS[] = {
    {350, {1, 31}, {1, 3}, {3, 1}, false},
    {650, {1, 10}, {1, 2}, {2, 1}, false},
    {100, {30, 71}, {4, 11}, {9, 6}, false},
    {380, {1, 6}, {1, 3}, {3, 1}, false},
    {500, {6, 14}, {1, 2}, {2, 1}, false},
    {450, {23, 1}, {1, 2}, {2, 1}, true},
    {150, {2, 62}, {1, 6}, {6, 1}, false},
    {200, {3, 130}, {7, 16}, {3, 16}, false},
    {200, {130, 7}, {16, 7}, {16, 3}, true},
    {365, {18, 1}, {3, 1}, {1, 3}, true},
    {270, {36, 1}, {1, 2}, {2, 1}, true},
    {320, {36, 1}, {1, 2}, {2, 1}, true},
};

#define RCSWITCH_PROTOCOLS_COUNT (sizeof(RCSWITCH_PROTOCOLS) / sizeof(RCSWITCH_PROTOCOLS[0]))
#define RCSWITCH_TOLERANCE_PCT   60
#define RCSWITCH_MIN_PULSES      10
#define RCSWITCH_MIN_DECODE_BITS 12
#define RCSWITCH_MAX_DECODE_BITS 32
#define RCSWITCH_MIN_DELAY_US    50
#define RCSWITCH_MAX_DELAY_US    1200
#define RCSWITCH_NAME_BUF_SIZE   32
#define RCSWITCH_STEP_SIZE       2
#define RCSWITCH_SYNC_OVERHEAD   2
#define RCSWITCH_SSCANF_EXPECTED 1

static bool
protocol_rcswitch_decode(const int32_t *raw_data, size_t count, subghz_data_t *out_data) {
  if (count < RCSWITCH_MIN_PULSES) {
    return false;
  }

  for (int p = 0; p < (int)RCSWITCH_PROTOCOLS_COUNT; p++) {
    const subghz_rcswitch_proto_t *pro = &RCSWITCH_PROTOCOLS[p];

    for (size_t i = 0; i < count - 1; i++) {
      bool first_is_high = (raw_data[i] > 0);
      bool second_is_high = (raw_data[i + 1] > 0);

      if (pro->is_inverted_signal) {
        if (first_is_high || !second_is_high) {
          continue;
        }
      } else {
        if (!first_is_high || second_is_high) {
          continue;
        }
      }

      uint32_t dur1 = (uint32_t)abs(raw_data[i]);
      uint32_t dur2 = (uint32_t)abs(raw_data[i + 1]);

      uint32_t sync_long_factor =
          (pro->syncFactor.low > pro->syncFactor.high) ? pro->syncFactor.low : pro->syncFactor.high;
      uint32_t dur_long = (pro->syncFactor.low > pro->syncFactor.high) ? dur2 : dur1;

      if (sync_long_factor == 0) {
        continue;
      }
      uint32_t delay = dur_long / sync_long_factor;
      if (delay < RCSWITCH_MIN_DELAY_US || delay > RCSWITCH_MAX_DELAY_US) {
        continue;
      }

      if (!subghz_check_pulse(raw_data[i], delay * pro->syncFactor.high, RCSWITCH_TOLERANCE_PCT) ||
          !subghz_check_pulse(
              raw_data[i + 1], delay * pro->syncFactor.low, RCSWITCH_TOLERANCE_PCT)) {
        continue;
      }

      uint32_t code = 0;
      int bit_count = 0;
      size_t k = i + RCSWITCH_SYNC_OVERHEAD;

      while (k < count - 1 && bit_count < RCSWITCH_MAX_DECODE_BITS) {
        bool p_high = (raw_data[k] > 0);
        bool g_high = (raw_data[k + 1] > 0);

        if (pro->is_inverted_signal) {
          if (p_high || !g_high) {
            break;
          }
        } else {
          if (!p_high || g_high) {
            break;
          }
        }

        if (subghz_check_pulse(raw_data[k], delay * pro->zero.high, RCSWITCH_TOLERANCE_PCT) &&
            subghz_check_pulse(raw_data[k + 1], delay * pro->zero.low, RCSWITCH_TOLERANCE_PCT)) {
          code <<= 1;
          bit_count++;
        } else if (subghz_check_pulse(raw_data[k], delay * pro->one.high, RCSWITCH_TOLERANCE_PCT) &&
                   subghz_check_pulse(
                       raw_data[k + 1], delay * pro->one.low, RCSWITCH_TOLERANCE_PCT)) {
          code = (code << 1) | 1;
          bit_count++;
        } else {
          break;
        }
        k += RCSWITCH_STEP_SIZE;
      }

      if (bit_count >= RCSWITCH_MIN_DECODE_BITS) {
        static char s_name_buf[RCSWITCH_NAME_BUF_SIZE];
        snprintf(s_name_buf, sizeof(s_name_buf), "RCSwitch(P%d)", p + 1);
        out_data->protocol_name = s_name_buf;
        out_data->bit_count = bit_count;
        out_data->raw_value = code;
        out_data->serial = code;
        out_data->btn = 0;
        ESP_LOGD(TAG, "Decoded %s", out_data->protocol_name);
        return true;
      }
    }
  }

  return false;
}

static size_t
protocol_rcswitch_encode(const subghz_data_t *data, int32_t *pulses, size_t max_count) {
  int p_idx = -1;
  if (data->protocol_name != NULL &&
      sscanf(data->protocol_name, "RCSwitch(P%d)", &p_idx) == RCSWITCH_SSCANF_EXPECTED) {
    p_idx -= 1;
  }

  if (p_idx < 0 || p_idx >= (int)RCSWITCH_PROTOCOLS_COUNT) {
    return 0;
  }
  const subghz_rcswitch_proto_t *pro = &RCSWITCH_PROTOCOLS[p_idx];

  if (max_count < (size_t)(data->bit_count * RCSWITCH_STEP_SIZE + RCSWITCH_SYNC_OVERHEAD)) {
    return 0;
  }

  uint32_t delay = pro->pulseLength;
  size_t idx = 0;

  if (pro->is_inverted_signal) {
    pulses[idx++] = -(int32_t)(delay * pro->syncFactor.high);
    pulses[idx++] = (int32_t)(delay * pro->syncFactor.low);
  } else {
    pulses[idx++] = (int32_t)(delay * pro->syncFactor.high);
    pulses[idx++] = -(int32_t)(delay * pro->syncFactor.low);
  }

  for (int i = data->bit_count - 1; i >= 0; i--) {
    bool bit = (data->raw_value >> i) & 1;
    if (bit) {
      if (pro->is_inverted_signal) {
        pulses[idx++] = -(int32_t)(delay * pro->one.high);
        pulses[idx++] = (int32_t)(delay * pro->one.low);
      } else {
        pulses[idx++] = (int32_t)(delay * pro->one.high);
        pulses[idx++] = -(int32_t)(delay * pro->one.low);
      }
    } else {
      if (pro->is_inverted_signal) {
        pulses[idx++] = -(int32_t)(delay * pro->zero.high);
        pulses[idx++] = (int32_t)(delay * pro->zero.low);
      } else {
        pulses[idx++] = (int32_t)(delay * pro->zero.high);
        pulses[idx++] = -(int32_t)(delay * pro->zero.low);
      }
    }
  }

  return idx;
}

subghz_protocol_t protocol_rcswitch = {
    .name = "RCSwitch", .decode = protocol_rcswitch_decode, .encode = protocol_rcswitch_encode};
