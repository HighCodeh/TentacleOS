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

typedef struct {
    const char* protocol_name;
    uint32_t serial;
    uint8_t btn;
    uint8_t bit_count;
    uint32_t raw_value;
} subghz_data_t;

#endif // SUBGHZ_TYPES_H
