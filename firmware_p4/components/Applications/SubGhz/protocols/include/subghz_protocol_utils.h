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

#ifndef SUBGHZ_PROTOCOL_UTILS_H
#define SUBGHZ_PROTOCOL_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

/**
 * @brief Calculate absolute difference between two values
 */
static inline uint32_t subghz_abs_diff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

/**
 * @brief Check if a pulse matches a target length within a tolerance percentage
 */
static inline bool subghz_check_pulse(int32_t raw_len, uint32_t target_len, uint8_t tolerance_pct) {
    uint32_t abs_len = abs(raw_len);
    uint32_t tolerance = target_len * tolerance_pct / 100;
    return (abs_len >= target_len - tolerance) && (abs_len <= target_len + tolerance);
}

#endif // SUBGHZ_PROTOCOL_UTILS_H
