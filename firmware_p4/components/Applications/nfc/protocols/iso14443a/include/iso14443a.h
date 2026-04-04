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
 * @file iso14443a.h
 * @brief ISO14443A types and CRC_A.
 */
#ifndef ISO14443A_H
#define ISO14443A_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_types.h"

/** Calculate CRC_A. Initial value 0x6363. */
void iso14443a_crc(const uint8_t* data, size_t len, uint8_t crc[2]);

/** Verify CRC_A on received data (last 2 bytes are CRC). */
bool iso14443a_check_crc(const uint8_t* data, size_t len);

#endif
