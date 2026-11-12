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
 * @file nfc_crypto.h
 * @brief Common crypto helpers for NFC phases (CMAC, KDF, diversification).
 */
#ifndef NFC_CRYPTO_H
#define NFC_CRYPTO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief MAC algorithm types for NFC crypto operations.
 */
typedef enum {
  NFC_MAC_AES_CMAC_16 = 0, /**< Full 16B CMAC (key diversification). */
  NFC_MAC_AES_CMAC_EV1_8,  /**< First 8B (DESFire EV1, MFP SL3). */
  NFC_MAC_AES_CMAC_EV2_8,  /**< Odd bytes (DESFire EV2/EV3). */
  NFC_MAC_3DES_CBC_8,      /**< 8B 3DES-CBC-MAC. */
  NFC_MAC_3DES_CBC_4,      /**< 4B 3DES-CBC-MAC (truncated). */
  NFC_MAC_COUNT
} nfc_mac_type_t;

/**
 * @brief Parameters for AES-128-CBC encrypt/decrypt operations.
 */
typedef struct {
  bool encrypt;         /**< true for encryption, false for decryption. */
  const uint8_t *key;   /**< 16-byte AES key. */
  const uint8_t *iv_in; /**< 16-byte initialization vector. */
  uint8_t *iv_out;      /**< Updated IV after the operation (may be NULL). */
} nfc_cbc_params_t;

/**
 * @brief Input parameters for MAC compute/verify operations.
 */
typedef struct {
  nfc_mac_type_t type; /**< MAC algorithm to use. */
  const uint8_t *key;  /**< Key bytes. */
  size_t key_len;      /**< Key length in bytes. */
  const uint8_t *data; /**< Input data. */
  size_t data_len;     /**< Length of the input data. */
} nfc_mac_input_t;

/**
 * @brief Session context for EV2 MAC computations.
 */
typedef struct {
  const uint8_t *ses_mac_key; /**< Session MAC key (16 bytes). */
  uint8_t ins;                /**< Command instruction byte. */
  uint16_t cmd_ctr;           /**< Command counter value. */
  const uint8_t *ti;          /**< Transaction identifier (4 bytes). */
} nfc_ev2_mac_ctx_t;

/**
 * @brief Command header and data for EV2 command MAC computation.
 */
typedef struct {
  const uint8_t *header; /**< Command header bytes (may be NULL). */
  size_t hdr_len;        /**< Length of the command header. */
  const uint8_t *data;   /**< Command data bytes (may be NULL). */
  size_t data_len;       /**< Length of the command data. */
} nfc_ev2_cmd_data_t;

/**
 * @brief Parameters for NXP AN10922 key diversification.
 */
typedef struct {
  const uint8_t *master_key; /**< Master key bytes. */
  const uint8_t *uid_7b;     /**< 7-byte UID of the card. */
  const uint8_t *aid_3b;     /**< 3-byte DESFire application ID (may be NULL). */
  uint8_t key_no;            /**< Key number within the application. */
  const uint8_t *system_id;  /**< System identifier bytes (may be NULL). */
  size_t system_id_len;      /**< Length of the system identifier. */
  const uint8_t *div_input;  /**< Diversification input data (2TDEA). */
  size_t div_input_len;      /**< Length of the diversification input (2TDEA). */
  const uint8_t
      *original_key; /**< Original key for XOR-based diversification (2TDEA, may be NULL). */
} nfc_diversify_params_t;

/**
 * @brief Constant-time memory comparison to prevent timing attacks.
 *
 * @param a  Pointer to the first buffer.
 * @param b  Pointer to the second buffer.
 * @param n  Number of bytes to compare.
 * @return 0 if buffers are equal, non-zero otherwise.
 */
int nfc_memcmp_ct(const void *a, const void *b, size_t n);

/**
 * @brief Encrypt a single 16-byte block using AES-128-ECB.
 *
 * @param key  16-byte AES key.
 * @param in   16-byte plaintext input block.
 * @param[out] out  16-byte ciphertext output block.
 * @return true on success, false on failure.
 */
bool nfc_aes_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

/**
 * @brief Encrypt or decrypt data using AES-128-CBC.
 *
 * @param p    CBC parameters (key, IV, encrypt flag, optional IV output).
 * @param in   Input data (must be a multiple of 16 bytes).
 * @param len  Length of the input data.
 * @param[out] out  Output buffer (same size as input).
 * @return true on success, false on failure.
 */
bool nfc_aes_cbc_crypt(const nfc_cbc_params_t *p, const uint8_t *in, size_t len, uint8_t *out);

/**
 * @brief Compute an AES-CMAC tag over a message.
 *
 * @param key      AES key bytes.
 * @param key_len  Key length in bytes (16).
 * @param msg      Message data.
 * @param msg_len  Length of the message.
 * @param[out] tag     16-byte CMAC output.
 * @return 0 on success, negative on failure.
 */
int nfc_aes_cmac(
    const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len, uint8_t tag[16]);

/**
 * @brief Compute a 3DES-CBC-MAC over data.
 *
 * @param key       3DES key bytes.
 * @param key_len   Key length in bytes (16 or 24).
 * @param data      Input data.
 * @param data_len  Length of the input data.
 * @param[out] mac_out  8-byte MAC output.
 * @return 0 on success, negative on failure.
 */
int nfc_tdes_cbc_mac(
    const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t mac_out[8]);

/**
 * @brief Compute a MAC using the specified algorithm type.
 *
 * @param in              MAC input parameters (type, key, data).
 * @param[out] mac_out      Buffer for the computed MAC.
 * @param[out] mac_len_out  Length of the MAC written.
 * @return 0 on success, negative on failure.
 */
int nfc_mac_compute(const nfc_mac_input_t *in, uint8_t *mac_out, size_t *mac_len_out);

/**
 * @brief Verify a received MAC against the expected value.
 *
 * @param in          MAC input parameters (type, key, data).
 * @param mac_rx      Received MAC bytes.
 * @param mac_rx_len  Length of the received MAC.
 * @return 0 if the MAC is valid, negative on mismatch or failure.
 */
int nfc_mac_verify(const nfc_mac_input_t *in, const uint8_t *mac_rx, size_t mac_rx_len);

/**
 * @brief Derive EV2 session MAC and encryption keys from the authentication exchange.
 *
 * @param kx       Base key (16 bytes).
 * @param rnd_a    Random challenge A (16 bytes).
 * @param rnd_b    Random challenge B (16 bytes).
 * @param[out] ses_mac  Derived 16-byte session MAC key.
 * @param[out] ses_enc  Derived 16-byte session encryption key.
 * @return true on success; false on invalid input or crypto failure (outputs cleared).
 */
bool nfc_ev2_derive_session_keys(const uint8_t *kx,
                                 const uint8_t *rnd_a,
                                 const uint8_t *rnd_b,
                                 uint8_t *ses_mac,
                                 uint8_t *ses_enc);

/**
 * @brief Truncate a 16-byte CMAC to the 8-byte EV2 format (odd-byte extraction).
 *
 * @param cmac16    Full 16-byte CMAC input.
 * @param[out] mac8     Truncated 8-byte MAC output.
 */
void nfc_ev2_mac_truncate(const uint8_t cmac16[16], uint8_t mac8[8]);

/**
 * @brief Compute the EV2 command MAC.
 *
 * @param ctx       EV2 MAC session context (key, ins, counter, TI).
 * @param cmd       Command header and data.
 * @param[out] mac8    Computed 8-byte MAC.
 * @return 0 on success, negative on failure.
 */
int nfc_ev2_compute_cmd_mac(const nfc_ev2_mac_ctx_t *ctx,
                            const nfc_ev2_cmd_data_t *cmd,
                            uint8_t mac8[8]);

/**
 * @brief Compute the EV2 response MAC for verification.
 *
 * @param ctx         EV2 MAC session context (key, ins, counter, TI).
 * @param resp_data   Response data bytes (may be NULL).
 * @param resp_len    Length of the response data.
 * @param[out] mac8       Computed 8-byte MAC.
 * @return 0 on success, negative on failure.
 */
int nfc_ev2_compute_resp_mac(const nfc_ev2_mac_ctx_t *ctx,
                             const uint8_t *resp_data,
                             size_t resp_len,
                             uint8_t mac8[8]);

/**
 * @brief Build the IV for EV2 command encryption.
 *
 * @param ses_enc   Session encryption key (16 bytes).
 * @param ti        Transaction identifier (4 bytes).
 * @param cmd_ctr   Command counter value.
 * @param[out] iv_out   Computed 16-byte IV.
 * @return true on success; false on invalid input or crypto failure (outputs cleared).
 */
bool nfc_ev2_build_iv_cmd(const uint8_t ses_enc[16],
                          const uint8_t ti[4],
                          uint16_t cmd_ctr,
                          uint8_t iv_out[16]);

/**
 * @brief Build the IV for EV2 response decryption.
 *
 * @param ses_enc       Session encryption key (16 bytes).
 * @param ti            Transaction identifier (4 bytes).
 * @param cmd_ctr_next  Command counter value after increment.
 * @param[out] iv_out       Computed 16-byte IV.
 * @return true on success; false on invalid input or crypto failure (outputs cleared).
 */
bool nfc_ev2_build_iv_resp(const uint8_t ses_enc[16],
                           const uint8_t ti[4],
                           uint16_t cmd_ctr_next,
                           uint8_t iv_out[16]);

/**
 * @brief Apply ISO 9797 padding method 2 (0x80 followed by zeros).
 *
 * @param in       Input data.
 * @param in_len   Length of the input data.
 * @param[out] out     Output buffer for padded data.
 * @param out_max  Maximum size of the output buffer.
 * @return Length of the padded data, or 0 if the buffer is too small.
 */
size_t nfc_iso9797_pad_method2(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max);

/**
 * @brief Remove ISO 9797 method 2 padding in place.
 *
 * @param buf  Buffer containing padded data.
 * @param len  Length of the padded data.
 * @return Length of the unpadded data.
 */
size_t nfc_iso9797_unpad_method2(uint8_t *buf, size_t len);

/**
 * @brief Apply ISO 9797 padding method 1 (zero padding).
 *
 * @param in       Input data.
 * @param in_len   Length of the input data.
 * @param[out] out     Output buffer for padded data.
 * @param out_max  Maximum size of the output buffer.
 * @return Length of the padded data, or 0 if the buffer is too small.
 */
size_t nfc_iso9797_pad_method1(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max);

/**
 * @brief Diversify an AES-128 master key using CMAC-based KDF.
 *
 * @param master_key     16-byte master key.
 * @param div_input      Diversification input data.
 * @param div_input_len  Length of the diversification input.
 * @param[out] div_key       16-byte diversified key output.
 * @return 0 on success, negative on failure.
 */
int nfc_diversify_aes128(const uint8_t *master_key,
                         const uint8_t *div_input,
                         size_t div_input_len,
                         uint8_t *div_key);

/**
 * @brief Diversify a DESFire key using NXP AN10922 method.
 *
 * @param p          Diversification parameters (master key, UID, AID, key_no, system_id).
 * @param[out] div_key   Diversified key output.
 * @return 0 on success, negative on failure.
 */
int nfc_diversify_desfire_key(const nfc_diversify_params_t *p, uint8_t *div_key);

/**
 * @brief Diversify a MIFARE Plus key for a specific block address.
 *
 * @param master_key  16-byte master key.
 * @param uid_7b      7-byte UID of the card.
 * @param block_addr  Block address for key diversification.
 * @param[out] div_key    16-byte diversified key output.
 * @return 0 on success, negative on failure.
 */
int nfc_diversify_mfp_key(const uint8_t *master_key,
                          const uint8_t *uid_7b,
                          uint16_t block_addr,
                          uint8_t *div_key);

/**
 * @brief Diversify a 2-key Triple DES key.
 *
 * @param p          Diversification parameters (master key, div_input, original_key).
 * @param[out] div_key   Diversified key output.
 * @return 0 on success, negative on failure.
 */
int nfc_diversify_2tdea(const nfc_diversify_params_t *p, uint8_t *div_key);

#ifdef __cplusplus
}
#endif

#endif /* NFC_CRYPTO_H */
