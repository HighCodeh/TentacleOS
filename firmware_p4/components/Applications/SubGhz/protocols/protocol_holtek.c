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

static const char *TAG = "PROTOCOL_HOLTEK";

#define HOLTEK_SHORT_US       430
#define HOLTEK_LONG_US        870
#define HOLTEK_TOLERANCE_PCT  40
#define HOLTEK_BIT_COUNT      12
#define HOLTEK_MIN_RAW_COUNT  24
#define HOLTEK_MIN_VALID_BITS 8
#define HOLTEK_STEP_SIZE      2
#define HOLTEK_BIT_INVALID    (-1)
#define HOLTEK_ADDR_SHIFT     4
#define HOLTEK_DATA_MASK      0x0F

static bool protocol_holtek_decode(const int32_t *raw_data, size_t count, subghz_data_t *out_data) {
  if (count < HOLTEK_MIN_RAW_COUNT) {
    return false;
  }

  size_t start = (raw_data[0] < 0) ? 1 : 0;
  uint32_t decoded_data = 0;
  int bits_found = 0;

  for (size_t i = start; i < count - 1; i += HOLTEK_STEP_SIZE) {
    int32_t pulse = raw_data[i];
    int32_t gap = raw_data[i + 1];

    if (pulse < 0) {
      return false;
    }

    int bit = HOLTEK_BIT_INVALID;

    if (subghz_check_pulse(pulse, HOLTEK_SHORT_US, HOLTEK_TOLERANCE_PCT) &&
        subghz_check_pulse(gap, HOLTEK_LONG_US, HOLTEK_TOLERANCE_PCT)) {
      bit = 0;
    } else if (subghz_check_pulse(pulse, HOLTEK_LONG_US, HOLTEK_TOLERANCE_PCT) &&
               subghz_check_pulse(gap, HOLTEK_SHORT_US, HOLTEK_TOLERANCE_PCT)) {
      bit = 1;
    } else {
      if (bits_found >= HOLTEK_MIN_VALID_BITS) {
        break;
      }
      decoded_data = 0;
      bits_found = 0;
      continue;
    }

    if (bit != HOLTEK_BIT_INVALID) {
      decoded_data = (decoded_data << 1) | bit;
      bits_found++;

      if (bits_found == HOLTEK_BIT_COUNT) {
        out_data->protocol_name = "Holtek";
        out_data->bit_count = HOLTEK_BIT_COUNT;
        out_data->raw_value = decoded_data;
        out_data->serial = decoded_data >> HOLTEK_ADDR_SHIFT;
        out_data->btn = decoded_data & HOLTEK_DATA_MASK;
        ESP_LOGD(TAG, "Decoded %s", out_data->protocol_name);
        return true;
      }
    }
  }
  return false;
}

subghz_protocol_t protocol_holtek = {
    .name = "Holtek", .decode = protocol_holtek_decode, .encode = NULL};
