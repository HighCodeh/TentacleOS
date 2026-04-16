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

// AWID — 26-bit Wiegand format
// Identical structure to HID H10301:
// Bit 25:     even parity (covers bits 25-13)
// Bits 24-17: 8-bit facility code (0-255)
// Bits 16-1:  16-bit card number (0-65535)
// Bit 0:      odd parity (covers bits 12-0)
// Distinguished from HID by the EM4100 customer code byte.
// AWID cards typically have customer code 0x01.

#include "rfid_protocol_decoder.h"

#include "esp_log.h"

static const char *TAG = "PROTO_AWID";

#define AWID_BITS        26
#define AWID_CUSTOMER_ID 0x01
#define AWID_FC_SHIFT    17
#define AWID_FC_MASK     0xFF
#define AWID_CARD_SHIFT  1
#define AWID_CARD_MASK   0xFFFF

static int count_bits(uint32_t value, int from_bit, int to_bit) {
  int count = 0;
  for (int i = from_bit; i <= to_bit; i++) {
    if (value & (1u << i)) {
      count++;
    }
  }
  return count;
}

static bool protocol_awid_decode(const ys_rfid2_raw_data_t *raw, rfid_decoded_data_t *out_data) {
  if (raw->bit_count != 40) {
    return false;
  }

  // Check AWID customer code in the upper byte
  if (raw->data[0] != AWID_CUSTOMER_ID) {
    return false;
  }

  uint64_t full_value = 0;
  for (int i = 0; i < YS_RFID2_RAW_DATA_LEN; i++) {
    full_value = (full_value << 8) | raw->data[i];
  }

  uint32_t wiegand = (uint32_t)(full_value & 0x03FFFFFF);

  uint8_t facility_code = (wiegand >> AWID_FC_SHIFT) & AWID_FC_MASK;
  uint16_t card_number = (wiegand >> AWID_CARD_SHIFT) & AWID_CARD_MASK;

  // Validate even parity (bits 25-13)
  if ((count_bits(wiegand, 13, 25) % 2) != 0) {
    return false;
  }

  // Validate odd parity (bits 12-0)
  if ((count_bits(wiegand, 0, 12) % 2) != 1) {
    return false;
  }

  out_data->protocol_name = "AWID";
  out_data->facility_code = facility_code;
  out_data->card_number = card_number;
  out_data->bit_count = AWID_BITS;
  out_data->raw_value = wiegand;

  ESP_LOGD(TAG, "FC: %u, Card: %u", facility_code, card_number);
  return true;
}

rfid_protocol_t protocol_awid = {
    .name = "AWID",
    .decode = protocol_awid_decode,
};
