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

#ifndef RFID_TYPES_H
#define RFID_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Decoded card data produced by protocol plugins.
 */
typedef struct {
  const char *protocol_name;
  uint32_t card_number;
  uint16_t facility_code;
  uint8_t bit_count;
  uint64_t raw_value;
} rfid_decoded_data_t;

#ifdef __cplusplus
}
#endif

#endif // RFID_TYPES_H
