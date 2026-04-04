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
 * @file t1t.h
 * @brief NFC Forum Type 1 Tag (Topaz) commands.
 */
#ifndef T1T_H
#define T1T_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

#define T1T_UID_LEN   7U
#define T1T_UID4_LEN  4U

typedef struct {
    uint8_t hr0;
    uint8_t hr1;
    uint8_t uid[T1T_UID_LEN];
    uint8_t uid_len;        /* 4 or 7 */
    bool    is_topaz512;
} t1t_tag_t;

/** Configure RF for NFC-A 106 kbps and enable field. */
hb_nfc_err_t t1t_poller_init(void);

/** Check if ATQA matches Topaz (Type 1). */
bool t1t_is_atqa(const uint8_t atqa[2]);

/** Send RID and fill HR0/HR1 + UID0..3. */
hb_nfc_err_t t1t_rid(t1t_tag_t* tag);

/** REQA + RID; validates ATQA=0x0C 0x00. */
hb_nfc_err_t t1t_select(t1t_tag_t* tag);

/** Read all memory (RALL). Output includes HR0..RES bytes (no CRC). */
hb_nfc_err_t t1t_rall(t1t_tag_t* tag, uint8_t* out, size_t out_max, size_t* out_len);

/** Read a single byte at address. */
hb_nfc_err_t t1t_read_byte(const t1t_tag_t* tag, uint8_t addr, uint8_t* data);

/** Write with erase (WRITE-E). */
hb_nfc_err_t t1t_write_e(const t1t_tag_t* tag, uint8_t addr, uint8_t data);

/** Write no erase (WRITE-NE). */
hb_nfc_err_t t1t_write_ne(const t1t_tag_t* tag, uint8_t addr, uint8_t data);

/** Topaz-512 only: read 8-byte block. */
hb_nfc_err_t t1t_read8(const t1t_tag_t* tag, uint8_t block, uint8_t out[8]);

/** Topaz-512 only: write 8-byte block with erase. */
hb_nfc_err_t t1t_write_e8(const t1t_tag_t* tag, uint8_t block, const uint8_t data[8]);

#endif
