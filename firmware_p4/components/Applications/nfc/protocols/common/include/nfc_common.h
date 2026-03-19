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
 * @file nfc_common.h
 * @brief Common utilities for the NFC stack.
 */
#ifndef NFC_COMMON_H
#define NFC_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"

/** Log a byte array as hex. */
void nfc_log_hex(const char* label, const uint8_t* data, size_t len);

#endif
