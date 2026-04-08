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

/**
 * @file tos_loot.h
 * @brief Unique loot filename generator with date prefix.
 */

#ifndef TOS_LOOT_H
#define TOS_LOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * @brief Generate a unique loot file path with date prefix.
 *
 * Uses YYYY-MM-DD if RTC/NTP is set, otherwise uptime-based.
 * Example: "2026-03-29_handshake_01.pcap" or "boot0142_handshake_01.pcap"
 *
 * Increments the index until a non-existing filename is found (max 99).
 *
 * @param dir        Target directory (e.g. TOS_PATH_WIFI_LOOT_HS).
 * @param prefix     File prefix (e.g. "handshake").
 * @param ext        Extension without dot (e.g. "pcap").
 * @param out        Output buffer for the full path.
 * @param out_size   Size of the out buffer.
 * @param out_name   Output buffer for filename only (can be NULL).
 * @param name_size  Size of the out_name buffer.
 */
void tos_loot_generate_path(const char *dir,
                            const char *prefix,
                            const char *ext,
                            char *out,
                            size_t out_size,
                            char *out_name,
                            size_t name_size);

#ifdef __cplusplus
}
#endif

#endif // TOS_LOOT_H
