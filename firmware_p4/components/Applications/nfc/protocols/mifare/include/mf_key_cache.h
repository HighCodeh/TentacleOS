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
#ifndef MF_KEY_CACHE_H
#define MF_KEY_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MF_KEY_CACHE_MAX_CARDS    16
#define MF_KEY_CACHE_MAX_SECTORS  40

/* One cached card entry: UID + all sector keys found. */
typedef struct {
    uint8_t  uid[10];
    uint8_t  uid_len;
    bool     key_a_known[MF_KEY_CACHE_MAX_SECTORS];
    bool     key_b_known[MF_KEY_CACHE_MAX_SECTORS];
    uint8_t  key_a[MF_KEY_CACHE_MAX_SECTORS][6];
    uint8_t  key_b[MF_KEY_CACHE_MAX_SECTORS][6];
    int      sector_count;
} mf_key_cache_entry_t;

void         mf_key_cache_init(void);
bool         mf_key_cache_lookup(const uint8_t* uid, uint8_t uid_len,
                                  int sector, mf_key_type_t type,
                                  uint8_t key_out[6]);
void         mf_key_cache_store(const uint8_t* uid, uint8_t uid_len,
                                 int sector, int sector_count,
                                 mf_key_type_t type,
                                 const uint8_t key[6]);
void         mf_key_cache_save(void);
void         mf_key_cache_clear_uid(const uint8_t* uid, uint8_t uid_len);
int          mf_key_cache_get_known_count(const uint8_t* uid, uint8_t uid_len);

#ifdef __cplusplus
}
#endif
#endif
