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

#ifndef SUBGHZ_ANALYZER_H
#define SUBGHZ_ANALYZER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint32_t estimated_te;       // base time element in microseconds 
    uint32_t pulse_min;
    uint32_t pulse_max;
    size_t pulse_count;
    const char* modulation_hint; // "PWM", "Manchester" ou "Unknown"
    uint8_t bitstream[128];      // buffer for espected bits
    size_t bitstream_len;        // quantity of recovered bits
} subghz_analyzer_result_t;

bool subghz_analyzer_process(const int32_t* pulses, size_t count, subghz_analyzer_result_t* out_result);

#endif // SUBGHZ_ANALYZER_H
