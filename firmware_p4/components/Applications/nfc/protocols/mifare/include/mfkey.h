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
 * @file mfkey.h
 * @brief MFKey key recovery attacks for MIFARE Classic.
 */
#ifndef MFKEY_H
#define MFKEY_H

#include <stdint.h>
#include <stdbool.h>

/** Recover key from two nonces (nested attack). */
bool mfkey32(uint32_t uid, uint32_t nt0, uint32_t nr0, uint32_t ar0,
             uint32_t nt1, uint32_t nr1, uint32_t ar1, uint64_t* key);

#endif
