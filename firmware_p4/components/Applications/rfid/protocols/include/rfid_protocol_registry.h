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

#ifndef RFID_PROTOCOL_REGISTRY_H
#define RFID_PROTOCOL_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rfid_protocol_decoder.h"

/**
 * @brief Initialize the protocol registry.
 */
void rfid_protocol_registry_init(void);

/**
 * @brief Run all registered decoders on raw card data.
 *
 * @param raw       Raw data from the RFID reader.
 * @param out_data  Pointer to store decoded results.
 * @return true if a protocol claimed the data.
 */
bool rfid_protocol_registry_decode_all(const ys_rfid2_raw_data_t *raw,
                                       rfid_decoded_data_t *out_data);

/**
 * @brief Find a protocol by name.
 *
 * @param name  Protocol name to search for.
 * @return Pointer to protocol structure, or NULL if not found.
 */
const rfid_protocol_t *rfid_protocol_registry_get_by_name(const char *name);

/**
 * @brief Get the number of registered protocols.
 */
size_t rfid_protocol_registry_get_count(void);

/**
 * @brief Get a protocol by index.
 *
 * @param index  Zero-based index into the registry.
 * @return Pointer to protocol structure, or NULL if index is out of range.
 */
const rfid_protocol_t *rfid_protocol_registry_get_by_index(size_t index);

#ifdef __cplusplus
}
#endif

#endif // RFID_PROTOCOL_REGISTRY_H
