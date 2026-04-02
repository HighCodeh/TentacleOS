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

#ifndef SUBGHZ_TYPES_H
#define SUBGHZ_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decoded Sub-GHz signal data.
 */
typedef struct {
  const char *protocol_name;  /**< @brief Name of the decoded protocol. */
  uint32_t serial;            /**< @brief Device serial number. */
  uint8_t btn;                /**< @brief Button code. */
  uint8_t bit_count;          /**< @brief Number of bits in the raw value. */
  uint32_t raw_value;         /**< @brief Raw decoded value. */
} subghz_data_t;

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_TYPES_H
