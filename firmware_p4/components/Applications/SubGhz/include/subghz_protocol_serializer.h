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

#ifndef SUBGHZ_PROTOCOL_SERIALIZER_H
#define SUBGHZ_PROTOCOL_SERIALIZER_H

#include "subghz_types.h"
#include <stddef.h>
#include <stdint.h>

uint8_t subghz_protocol_get_preset_id(void);
size_t subghz_protocol_serialize_decoded(const subghz_data_t* data, uint32_t frequency, uint32_t te, char* out_buf, size_t out_size);
size_t subghz_protocol_serialize_raw(const int32_t* pulses, size_t count, uint32_t frequency, char* out_buf, size_t out_size);
size_t subghz_protocol_parse_raw(const char* content, int32_t* out_pulses, size_t max_count, uint32_t* out_frequency, uint8_t* out_preset);

#endif // SUBGHZ_PROTOCOL_SERIALIZER_H
