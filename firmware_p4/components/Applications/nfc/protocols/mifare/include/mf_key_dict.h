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
 * @file mf_key_dict.h
 * @brief MIFARE Classic key dictionary (built-in + user-added keys).
 */
#ifndef MF_KEY_DICT_H
#define MF_KEY_DICT_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum user-added keys stored in NVS (on top of built-in). */
#define MF_KEY_DICT_MAX_EXTRA 128

/** @brief Total built-in keys count (defined in .c). */
extern const int MF_KEY_DICT_BUILTIN_COUNT;

/**
 * @brief Initialize the key dictionary (load user keys from NVS).
 */
void mf_key_dict_init(void);

/**
 * @brief Get total number of keys (built-in + user-added).
 *
 * @return Total key count.
 */
int mf_key_dict_count(void);

/**
 * @brief Get a key by index.
 *
 * @param idx          Key index (0-based).
 * @param[out] key_out 6-byte key.
 */
void mf_key_dict_get(int idx, uint8_t key_out[6]);

/**
 * @brief Check if a key exists in the dictionary.
 *
 * @param key  6-byte key to check.
 * @return true if the key is in the dictionary.
 */
bool mf_key_dict_contains(const uint8_t key[6]);

/**
 * @brief Add a user key to the dictionary.
 *
 * @param key  6-byte key to add.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code if dictionary is full or key already exists
 */
hb_nfc_err_t mf_key_dict_add(const uint8_t key[6]);

#ifdef __cplusplus
}
#endif
#endif
