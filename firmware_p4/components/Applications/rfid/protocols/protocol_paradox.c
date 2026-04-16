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

// Paradox — Proprietary security system format
// Uses the full 40 bits from EM4100:
// Bits [39:32] = 8-bit facility/site code
// Bits [31:16] = 16-bit card number
// Bits [15:0]  = 16-bit checksum/serial
// Distinguished by EM4100 customer code 0x05.

#include "rfid_protocol_decoder.h"

#include "esp_log.h"

static const char *TAG = "PROTO_PARADOX";

#define PARADOX_BITS        40
#define PARADOX_CUSTOMER_ID 0x05
#define PARADOX_FC_SHIFT    24
#define PARADOX_FC_MASK     0xFF
#define PARADOX_CARD_SHIFT  8
#define PARADOX_CARD_MASK   0xFFFF

static bool protocol_paradox_decode(const ys_rfid2_raw_data_t *raw,
                                    rfid_decoded_data_t *out_data) {
  if (raw->bit_count != 40) {
    return false;
  }

  if (raw->data[0] != PARADOX_CUSTOMER_ID) {
    return false;
  }

  uint64_t full_value = 0;
  for (int i = 0; i < YS_RFID2_RAW_DATA_LEN; i++) {
    full_value = (full_value << 8) | raw->data[i];
  }

  // Strip customer code byte, work with lower 32 bits
  uint32_t payload = (uint32_t)(full_value & 0xFFFFFFFF);

  out_data->protocol_name = "Paradox";
  out_data->facility_code = (payload >> PARADOX_FC_SHIFT) & PARADOX_FC_MASK;
  out_data->card_number = (payload >> PARADOX_CARD_SHIFT) & PARADOX_CARD_MASK;
  out_data->bit_count = PARADOX_BITS;
  out_data->raw_value = full_value;

  ESP_LOGD(TAG, "FC: %u, Card: %lu",
      out_data->facility_code, (unsigned long)out_data->card_number);
  return true;
}

rfid_protocol_t protocol_paradox = {
    .name = "Paradox",
    .decode = protocol_paradox_decode,
};
