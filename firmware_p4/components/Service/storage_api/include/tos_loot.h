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

#ifndef TOS_LOOT_H
#define TOS_LOOT_H

#include <stddef.h>

// Generates a unique loot filename with date prefix.
// Uses YYYY-MM-DD if RTC/NTP is set, otherwise uptime-based.
// Example: "2026-03-29_handshake_01.pcap" or "boot0142_handshake_01.pcap"
//
// dir:    target directory (e.g. TOS_PATH_WIFI_LOOT_HS)
// prefix: file prefix (e.g. "handshake")
// ext:    extension (e.g. "pcap")
// out:    output buffer for full path
// out_name: output buffer for filename only (can be NULL)
// out_size: size of out buffer
// name_size: size of out_name buffer
void tos_loot_generate_path(const char *dir, const char *prefix, const char *ext,
                            char *out, size_t out_size,
                            char *out_name, size_t name_size);

#endif // TOS_LOOT_H
