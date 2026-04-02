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

#ifndef SUBGHZ_PROTOCOL_REGISTRY_H
#define SUBGHZ_PROTOCOL_REGISTRY_H

#include "subghz_protocol_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the protocol registry.
 */
void subghz_protocol_registry_init(void);

/**
 * @brief Run all registered decoders on the signal.
 *
 * @param pulses    Array of signed pulse durations in microseconds.
 * @param count     Number of elements in the pulses array.
 * @param out_data  Pointer to store decoded results.
 * @return true if a protocol claimed the signal.
 */
bool subghz_protocol_registry_decode_all(const int32_t *pulses, size_t count, subghz_data_t *out_data);

/**
 * @brief Find a protocol by name.
 *
 * @param name  Protocol name to search for.
 * @return Pointer to protocol structure, or NULL if not found.
 */
const subghz_protocol_t *subghz_protocol_registry_get_by_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_PROTOCOL_REGISTRY_H
