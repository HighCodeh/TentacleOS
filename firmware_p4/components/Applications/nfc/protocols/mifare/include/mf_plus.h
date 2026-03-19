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
 * @file mf_plus.h
 * @brief MiFARE Plus SL3 auth and crypto helpers.
 */
#ifndef MF_PLUS_H
#define MF_PLUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"
#include "iso_dep.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    nfc_iso_dep_data_t dep;
    bool               dep_ready;
    bool               authenticated;
    uint8_t            ses_enc[16];
    uint8_t            ses_auth[16];
    uint8_t            iv_enc[16];
    uint8_t            iv_mac[16];
} mfp_session_t;

hb_nfc_err_t mfp_poller_init(const nfc_iso14443a_data_t* card,
                             mfp_session_t*             session);

int mfp_apdu_transceive(mfp_session_t* session,
                        const uint8_t*  apdu,
                        size_t          apdu_len,
                        uint8_t*        rx,
                        size_t          rx_max,
                        int             timeout_ms);

uint16_t mfp_key_block_addr(uint8_t sector, bool key_b);

void mfp_sl3_derive_session_keys(const uint8_t* rnd_a,
                                 const uint8_t* rnd_b,
                                 uint8_t*       ses_enc,
                                 uint8_t*       ses_auth);

hb_nfc_err_t mfp_sl3_auth_first(mfp_session_t* session,
                               uint16_t       block_addr,
                               const uint8_t  key[16]);

hb_nfc_err_t mfp_sl3_auth_nonfirst(mfp_session_t* session,
                                  uint16_t       block_addr,
                                  const uint8_t  key[16]);

int mfp_sl3_compute_mac(const mfp_session_t* session,
                        const uint8_t*       data,
                        size_t               data_len,
                        uint8_t              mac8[8]);

size_t mfp_sl3_encrypt(mfp_session_t* session,
                       const uint8_t* plain,
                       size_t plain_len,
                       uint8_t* out,
                       size_t out_max);

size_t mfp_sl3_decrypt(mfp_session_t* session,
                       const uint8_t* enc,
                       size_t enc_len,
                       uint8_t* out,
                       size_t out_max);

#ifdef __cplusplus
}
#endif

#endif /* MF_PLUS_H */
