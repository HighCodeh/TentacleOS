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
 * @file mf_classic.h
 * @brief MIFARE Classic auth, read, write sectors.
 *
 * MIFARE Classic authentication and data access helpers.
 */
#ifndef MF_CLASSIC_H
#define MF_CLASSIC_H

#include <stdint.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "crypto1.h"

/** Authenticate a sector with key A or B. */
hb_nfc_err_t mf_classic_auth(uint8_t block, mf_key_type_t key_type,
                               const mf_classic_key_t* key,
                               const uint8_t uid[4]);

/** Read a single block (16 bytes). Must be authenticated first. */
hb_nfc_err_t mf_classic_read_block(uint8_t block, uint8_t data[16]);

/** Write a single block (16 bytes). Must be authenticated first. */
hb_nfc_err_t mf_classic_write_block(uint8_t block, const uint8_t data[16]);

/** Write phase (for debugging NACKs). */
typedef enum {
    MF_WRITE_PHASE_NONE = 0,
    MF_WRITE_PHASE_CMD,
    MF_WRITE_PHASE_DATA,
} mf_write_phase_t;

/** Get last write phase reached (CMD or DATA). */
mf_write_phase_t mf_classic_get_last_write_phase(void);

/** Get card type from SAK. */
mf_classic_type_t mf_classic_get_type(uint8_t sak);

/** Get number of sectors for a given type. */
int mf_classic_get_sector_count(mf_classic_type_t type);

/** Reset auth state (call before re-select). */
void mf_classic_reset_auth(void);

/** Get the last nonce (nt) received from card during auth.
 *  Used for PRNG analysis / clone detection. */
uint32_t mf_classic_get_last_nt(void);

/**
 * Copy the current Crypto1 cipher state (after a successful mf_classic_auth).
 * Used by the nested attack module to drive its own keystream without
 * touching the static s_crypto that mf_classic_read/write_block use.
 *
 * @param out  Destination state struct; must not be NULL.
 */
void mf_classic_get_crypto_state(crypto1_state_t* out);

#endif
