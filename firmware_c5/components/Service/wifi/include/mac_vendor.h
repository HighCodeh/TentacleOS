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

#ifndef MAC_VENDOR_H
#define MAC_VENDOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Look up the vendor name for a MAC address.
 *
 * Returns "Randomized" for locally administered MACs (bit 1 of first octet
 * set), or an empty string if the OUI is not in the lookup table.
 *
 * @param mac  Pointer to the 6-byte MAC address.
 * @return Vendor name string. Valid for the lifetime of the program.
 */
const char *mac_vendor_get_name(const uint8_t *mac);

#ifdef __cplusplus
}
#endif

#endif // MAC_VENDOR_H
