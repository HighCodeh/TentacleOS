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

#include <stdlib.h>

#include "esp_log.h"

#include "subghz_protocol_utils.h"

static const char *TAG = "PROTOCOL_ROSSI";

// Rossi (HCS301) Protocol Implementation
//
// Te (Elementary Period) is typically 300us - 400us.
// Short = 1 * Te, Long = 2 * Te.

#define ROSSI_SHORT_MIN_US       200
#define ROSSI_SHORT_MAX_US       600
#define ROSSI_LONG_MIN_US        601
#define ROSSI_LONG_MAX_US        1000
#define ROSSI_HEADER_MIN_US      3500
#define ROSSI_MIN_RAW_COUNT      60
#define ROSSI_HEADER_SEARCH_GAP  10
#define ROSSI_MIN_VALID_PULSES   50
#define ROSSI_BIT_COUNT          66
#define ROSSI_PLACEHOLDER_SERIAL 0xDEADBEEF

static bool is_short(int32_t val) {
  uint32_t a = abs(val);
  return (a >= ROSSI_SHORT_MIN_US && a <= ROSSI_SHORT_MAX_US);
}

static bool is_long(int32_t val) {
  uint32_t a = abs(val);
  return (a >= ROSSI_LONG_MIN_US && a <= ROSSI_LONG_MAX_US);
}

static bool protocol_rossi_decode(const int32_t *raw_data, size_t count, subghz_data_t *out_data) {
  if (count < ROSSI_MIN_RAW_COUNT) {
    return false;
  }

  size_t start_idx = 0;
  bool is_header_found = false;

  for (size_t i = 0; i < count - ROSSI_HEADER_SEARCH_GAP; i++) {
    if (abs(raw_data[i]) > ROSSI_HEADER_MIN_US) {
      start_idx = i + 1;
      is_header_found = true;
      break;
    }
  }

  if (is_header_found == false) {
    return false;
  }

  int valid_pulses = 0;

  for (size_t i = start_idx; i < count; i++) {
    int32_t t = raw_data[i];

    if (is_short(t) || is_long(t)) {
      valid_pulses++;
    } else {
      break;
    }
  }

  if (valid_pulses > ROSSI_MIN_VALID_PULSES) {
    out_data->protocol_name = "Rossi (HCS301)";
    out_data->bit_count = ROSSI_BIT_COUNT;
    out_data->serial = ROSSI_PLACEHOLDER_SERIAL;
    out_data->btn = 0;
    out_data->raw_value = 0;

    ESP_LOGD(TAG, "Decoded %s", out_data->protocol_name);
    return true;
  }

  return false;
}

subghz_protocol_t protocol_rossi = {
    .name = "Rossi", .decode = protocol_rossi_decode, .encode = NULL};
