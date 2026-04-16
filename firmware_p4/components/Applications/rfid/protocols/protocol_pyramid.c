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

// Pyramid / Farpointe Data — 26-bit format
// Bit 25:     even parity (covers bits 25-13)
// Bits 24-17: 8-bit facility code (0-255)
// Bits 16-1:  16-bit card number (0-65535)
// Bit 0:      odd parity (covers bits 12-0)
// Distinguished from HID by EM4100 customer code 0x02.

#include "rfid_protocol_decoder.h"

#include "esp_log.h"

static const char *TAG = "PROTO_PYRAMID";

#define PYRAMID_BITS        26
#define PYRAMID_CUSTOMER_ID 0x02
#define PYRAMID_FC_SHIFT    17
#define PYRAMID_FC_MASK     0xFF
#define PYRAMID_CARD_SHIFT  1
#define PYRAMID_CARD_MASK   0xFFFF

static int count_bits(uint32_t value, int from_bit, int to_bit) {
  int count = 0;
  for (int i = from_bit; i <= to_bit; i++) {
    if (value & (1u << i)) {
      count++;
    }
  }
  return count;
}

static bool protocol_pyramid_decode(const ys_rfid2_raw_data_t *raw,
                                    rfid_decoded_data_t *out_data) {
  if (raw->bit_count != 40) {
    return false;
  }

  if (raw->data[0] != PYRAMID_CUSTOMER_ID) {
    return false;
  }

  uint64_t full_value = 0;
  for (int i = 0; i < YS_RFID2_RAW_DATA_LEN; i++) {
    full_value = (full_value << 8) | raw->data[i];
  }

  uint32_t wiegand = (uint32_t)(full_value & 0x03FFFFFF);

  if ((count_bits(wiegand, 13, 25) % 2) != 0) {
    return false;
  }
  if ((count_bits(wiegand, 0, 12) % 2) != 1) {
    return false;
  }

  out_data->protocol_name = "Pyramid";
  out_data->facility_code = (wiegand >> PYRAMID_FC_SHIFT) & PYRAMID_FC_MASK;
  out_data->card_number = (wiegand >> PYRAMID_CARD_SHIFT) & PYRAMID_CARD_MASK;
  out_data->bit_count = PYRAMID_BITS;
  out_data->raw_value = wiegand;

  ESP_LOGD(TAG, "FC: %u, Card: %lu",
      out_data->facility_code, (unsigned long)out_data->card_number);
  return true;
}

rfid_protocol_t protocol_pyramid = {
    .name = "Pyramid",
    .decode = protocol_pyramid_decode,
};
