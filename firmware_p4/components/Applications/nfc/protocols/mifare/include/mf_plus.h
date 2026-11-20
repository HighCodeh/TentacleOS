// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
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

#define MFP_BLOCK_SIZE      16U
#define MFP_MAC_SIZE        8U
#define MFP_TI_SIZE         4U
#define MFP_MAX_READ_BLOCKS 15U

/**
 * @brief MIFARE Plus session state.
 */
typedef struct {
  nfc_iso_dep_data_t dep;
  bool dep_ready;
  bool authenticated;
  uint8_t ti[MFP_TI_SIZE];
  uint16_t read_counter;
  uint16_t write_counter;
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
 * @param key           Authentication AES key (16 bytes).
 * @param rnd_a         Random number A (16 bytes).
 * @param rnd_b         Random number B (16 bytes).
 * @param[out] ses_enc  Derived session encryption key (16 bytes).
 * @param[out] ses_auth Derived session authentication key (16 bytes).
 */
hb_nfc_err_t mfp_sl3_derive_session_keys(const uint8_t *key,
                                         const uint8_t *rnd_a,
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

#define MFP_VERSION_SIZE           28U
#define MFP_SIGNATURE_SIZE         56U
#define MFP_LISTENER_RESPONSE_SIZE (MFP_MAX_READ_BLOCKS * MFP_BLOCK_SIZE + MFP_MAC_SIZE + 2U)

typedef bool (*mfp_key_cb_t)(void *context, uint16_t address, uint8_t out_key[MFP_BLOCK_SIZE]);
typedef bool (*mfp_read_cb_t)(void *context,
                              uint16_t key_address,
                              uint16_t address,
                              uint8_t out_block[MFP_BLOCK_SIZE]);
typedef bool (*mfp_write_cb_t)(void *context,
                               uint16_t key_address,
                               uint16_t block_address,
                               const uint8_t block[MFP_BLOCK_SIZE]);
typedef struct {
  void *context;
  mfp_key_cb_t key;
  mfp_read_cb_t read;
  mfp_write_cb_t write;
  const uint8_t *version;
  const uint8_t *signature;
} mfp_listener_config_t;

typedef struct {
  mfp_listener_config_t config;
  mfp_session_t session;
  uint8_t auth_key[MFP_BLOCK_SIZE];
  uint8_t rnd_b[MFP_BLOCK_SIZE];
  uint16_t key_address;
  uint8_t version_stage;
  bool has_challenge;
  bool has_auth_key;
  bool is_first;
  bool is_version_wrapped;
} mfp_listener_t;

/* Single-owner protocol engine. Call reset on field loss, HALT or deselection.
 * Configuration pointers remain owned by the caller and must outlive the engine.
 * Callbacks expose only recovered data. The write callback enforces card access
 * conditions using the authenticated key address; NULL disables writes. */
hb_nfc_err_t mfp_listener_init(mfp_listener_t *listener, const mfp_listener_config_t *config);
void mfp_listener_reset(mfp_listener_t *listener);
hb_nfc_err_t mfp_listener_exchange(mfp_listener_t *listener,
                                   const uint8_t *request,
                                   size_t length,
                                   uint8_t *out,
                                   size_t capacity,
                                   size_t *out_len);

hb_nfc_err_t mfp_get_version(mfp_session_t *session, uint8_t out[MFP_VERSION_SIZE]);
hb_nfc_err_t mfp_read_signature(mfp_session_t *session, uint8_t out[MFP_SIGNATURE_SIZE]);

/* Native SL3 commands; MAC is required in both plaintext and encrypted mode.
 * Read supports 1..15 blocks; write is exactly one block, never auto-retried.
 * An uncertain exchange invalidates authentication and requires a new handshake. */
hb_nfc_err_t mfp_sl3_read(mfp_session_t *session,
                          uint16_t address,
                          uint8_t blocks,
                          bool is_encrypted,
                          uint8_t *out,
                          size_t capacity,
                          size_t *out_len);
hb_nfc_err_t mfp_sl3_write(mfp_session_t *session,
                           uint16_t address,
                           bool is_encrypted,
                           const uint8_t *data,
                           size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* MF_PLUS_H */
