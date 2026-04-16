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

#ifndef RFID_PROTOCOL_DECODER_H
#define RFID_PROTOCOL_DECODER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ys_rfid2_types.h"
#include "rfid_types.h"

/**
 * @brief Interface for RFID 125kHz protocol decoders.
 */
typedef struct {
  const char *name;

  /**
   * @brief Decode raw card data into structured fields.
   *
   * @param raw       Raw data from the RFID reader (40-bit card ID).
   * @param out_data  Pointer to store decoded results.
   * @return true if the protocol recognized the data.
   */
  bool (*decode)(const ys_rfid2_raw_data_t *raw, rfid_decoded_data_t *out_data);
} rfid_protocol_t;

#ifdef __cplusplus
}
#endif

#endif // RFID_PROTOCOL_DECODER_H
