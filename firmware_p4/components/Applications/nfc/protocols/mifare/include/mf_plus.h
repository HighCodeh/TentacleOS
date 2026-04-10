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
 * @brief MIFARE Plus SL3 auth and crypto helpers.
 */
#ifndef MF_PLUS_H
#define MF_PLUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"
#include "highboy_nfc_types.h"
#include "iso_dep.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MIFARE Plus session state.
 */
typedef struct {
  nfc_iso_dep_data_t dep;
  bool dep_ready;
  bool authenticated;
  uint8_t ses_enc[16];
  uint8_t ses_auth[16];
  uint8_t iv_enc[16];
  uint8_t iv_mac[16];
} mfp_session_t;

/**
 * @brief Initialize MIFARE Plus poller session with ISO-DEP activation.
 *
 * @param card     ISO 14443A card data.
 * @param[out] session  Session state to initialize.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mfp_poller_init(const nfc_iso14443a_data_t *card, mfp_session_t *session);

/**
 * @brief Transceive a raw APDU via ISO-DEP.
 *
 * @param session     Active session.
 * @param apdu        APDU buffer to send.
 * @param apdu_len    Length of APDU.
 * @param[out] rx     Receive buffer.
 * @param rx_max      Maximum receive buffer size.
 * @param timeout_ms  Timeout in milliseconds.
 * @return Number of bytes received, or negative on error.
 */
int mfp_apdu_transceive(mfp_session_t *session,
                        const uint8_t *apdu,
                        size_t apdu_len,
                        uint8_t *rx,
                        size_t rx_max,
                        int timeout_ms);

/**
 * @brief Compute key block address for a sector.
 *
 * @param sector  Sector number.
 * @param key_b   true for Key B, false for Key A.
 * @return Key block address.
 */
uint16_t mfp_key_block_addr(uint8_t sector, bool key_b);

/**
 * @brief Derive SL3 session keys from authentication random numbers.
 *
 * @param rnd_a         Random number A (16 bytes).
 * @param rnd_b         Random number B (16 bytes).
 * @param[out] ses_enc  Derived session encryption key (16 bytes).
 * @param[out] ses_auth Derived session authentication key (16 bytes).
 */
void mfp_sl3_derive_session_keys(const uint8_t *rnd_a,
                                 const uint8_t *rnd_b,
                                 uint8_t *ses_enc,
                                 uint8_t *ses_auth);

/**
 * @brief Perform SL3 first authentication.
 *
 * @param session     Active session.
 * @param block_addr  Key block address.
 * @param key         16-byte AES key.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mfp_sl3_auth_first(mfp_session_t *session, uint16_t block_addr, const uint8_t key[16]);

/**
 * @brief Perform SL3 non-first (following) authentication.
 *
 * @param session     Active session.
 * @param block_addr  Key block address.
 * @param key         16-byte AES key.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t
mfp_sl3_auth_nonfirst(mfp_session_t *session, uint16_t block_addr, const uint8_t key[16]);

/**
 * @brief Compute an 8-byte MAC over data using session authentication key.
 *
 * @param session   Active session.
 * @param data      Data to MAC.
 * @param data_len  Length of data.
 * @param[out] mac8 8-byte MAC output.
 * @return Number of bytes written, or negative on error.
 */
int mfp_sl3_compute_mac(const mfp_session_t *session,
                        const uint8_t *data,
                        size_t data_len,
                        uint8_t mac8[8]);

/**
 * @brief Encrypt plaintext using session encryption key.
 *
 * @param session    Active session.
 * @param plain      Plaintext data.
 * @param plain_len  Length of plaintext.
 * @param[out] out   Output buffer for ciphertext.
 * @param out_max    Maximum output buffer size.
 * @return Number of ciphertext bytes written.
 */
size_t mfp_sl3_encrypt(
    mfp_session_t *session, const uint8_t *plain, size_t plain_len, uint8_t *out, size_t out_max);

/**
 * @brief Decrypt ciphertext using session encryption key.
 *
 * @param session   Active session.
 * @param enc       Ciphertext data.
 * @param enc_len   Length of ciphertext.
 * @param[out] out  Output buffer for plaintext.
 * @param out_max   Maximum output buffer size.
 * @return Number of plaintext bytes written.
 */
size_t mfp_sl3_decrypt(
    mfp_session_t *session, const uint8_t *enc, size_t enc_len, uint8_t *out, size_t out_max);

#ifdef __cplusplus
}
#endif

#endif /* MF_PLUS_H */
