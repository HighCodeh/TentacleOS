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
#ifndef MF_KEY_DICT_H
#define MF_KEY_DICT_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum user-added keys stored in NVS (on top of built-in). */
#define MF_KEY_DICT_MAX_EXTRA   128

/* Total built-in keys count (defined in .c). */
extern const int MF_KEY_DICT_BUILTIN_COUNT;

void         mf_key_dict_init(void);
int          mf_key_dict_count(void);
void         mf_key_dict_get(int idx, uint8_t key_out[6]);
bool         mf_key_dict_contains(const uint8_t key[6]);
hb_nfc_err_t mf_key_dict_add(const uint8_t key[6]);

#ifdef __cplusplus
}
#endif
#endif
