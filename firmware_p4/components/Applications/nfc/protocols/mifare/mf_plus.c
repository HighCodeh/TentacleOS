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
#include "mf_plus.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

#include "nfc_crypto.h"

#define MFP_AUTH_FIRST        0x70U
#define MFP_AUTH_NONFIRST     0x76U
#define MFP_AUTH_CONTINUE     0x72U
#define MFP_STATUS_OK         0x90U
#define MFP_READ_ENCRYPTED    0x31U
#define MFP_READ_PLAIN        0x33U
#define MFP_WRITE_ENCRYPTED   0xA1U
#define MFP_WRITE_PLAIN       0xA3U
#define MFP_KEY_BASE          0x4000U
#define MFP_CONFIG_BASE       0xB000U
#define MFP_RATS_FSDI         8U
#define MFP_TIMEOUT_MS        500
#define MFP_MAC_HEADER        7U
#define MFP_AUTH_TOKEN_SIZE   32U
#define MFP_AUTH_REPLY_SIZE   33U
#define MFP_DERIVE_PART       5U
#define MFP_ENC_MARKER        0x11U
#define MFP_MAC_MARKER        0x22U
#define MFP_DERIVE_ENC_OFFSET 11U
#define MFP_DERIVE_MAC_OFFSET 7U
#define MFP_DERIVE_XOR_OFFSET 4U
#define MFP_COUNTER_REPEAT    3U
#define MFP_BYTE_SHIFT        8U

static const char *TAG = "NFC_MF_PLUS";

static void clear_auth(mfp_session_t *session) {
  session->authenticated = false;
  memset(session->ses_enc, 0, sizeof(session->ses_enc));
  memset(session->ses_auth, 0, sizeof(session->ses_auth));
  memset(session->iv_enc, 0, sizeof(session->iv_enc));
  memset(session->iv_mac, 0, sizeof(session->iv_mac));
}

static hb_nfc_err_t fail_session(mfp_session_t *session, hb_nfc_err_t err) {
  clear_auth(session);
  ESP_LOGD(TAG, "SL3 session invalidated: %d", (int)err);
  return err;
}

static void rotate_left(uint8_t *out, const uint8_t *in) {
  memcpy(out, in + 1, MFP_BLOCK_SIZE - 1);
  out[MFP_BLOCK_SIZE - 1] = in[0];
}

hb_nfc_err_t mfp_poller_init(const nfc_iso14443a_data_t *card, mfp_session_t *session) {
  if (card == NULL || session == NULL)
    return HB_NFC_ERR_PARAM;
  memset(session, 0, sizeof(*session));
  hb_nfc_err_t err = iso_dep_rats(MFP_RATS_FSDI, 0, &session->dep);
  session->dep_ready = err == HB_NFC_OK;
  return err;
}

int mfp_apdu_transceive(mfp_session_t *session,
                        const uint8_t *apdu,
                        size_t apdu_len,
                        uint8_t *rx,
                        size_t rx_max,
                        int timeout_ms) {
  if (session == NULL || !session->dep_ready || apdu == NULL || rx == NULL)
    return 0;
  return iso_dep_apdu_transceive(&session->dep, apdu, apdu_len, rx, rx_max, timeout_ms);
}

uint16_t mfp_key_block_addr(uint8_t sector, bool key_b) {
  return (uint16_t)(MFP_KEY_BASE + (uint16_t)sector * 2U + (key_b ? 1U : 0U));
}

hb_nfc_err_t mfp_sl3_derive_session_keys(const uint8_t *key,
                                         const uint8_t *rnd_a,
                                         const uint8_t *rnd_b,
                                         uint8_t *ses_enc,
                                         uint8_t *ses_auth) {
  if (key == NULL || rnd_a == NULL || rnd_b == NULL || ses_enc == NULL || ses_auth == NULL)
    return HB_NFC_ERR_PARAM;
  uint8_t vector[MFP_BLOCK_SIZE], enc[MFP_BLOCK_SIZE], mac[MFP_BLOCK_SIZE];
  memcpy(vector, rnd_a + MFP_DERIVE_ENC_OFFSET, MFP_DERIVE_PART);
  memcpy(vector + MFP_DERIVE_PART, rnd_b + MFP_DERIVE_ENC_OFFSET, MFP_DERIVE_PART);
  for (size_t i = 0; i < MFP_DERIVE_PART; i++)
    vector[2 * MFP_DERIVE_PART + i] =
        rnd_a[MFP_DERIVE_XOR_OFFSET + i] ^ rnd_b[MFP_DERIVE_XOR_OFFSET + i];
  vector[MFP_BLOCK_SIZE - 1] = MFP_ENC_MARKER;
  if (!nfc_aes_ecb_encrypt(key, vector, enc))
    return HB_NFC_ERR_INTERNAL;
  memcpy(vector, rnd_a + MFP_DERIVE_MAC_OFFSET, MFP_DERIVE_PART);
  memcpy(vector + MFP_DERIVE_PART, rnd_b + MFP_DERIVE_MAC_OFFSET, MFP_DERIVE_PART);
  for (size_t i = 0; i < MFP_DERIVE_PART; i++)
    vector[2 * MFP_DERIVE_PART + i] = rnd_a[i] ^ rnd_b[i];
  vector[MFP_BLOCK_SIZE - 1] = MFP_MAC_MARKER;
  if (!nfc_aes_ecb_encrypt(key, vector, mac))
    return HB_NFC_ERR_INTERNAL;
  memcpy(ses_enc, enc, sizeof(enc));
  memcpy(ses_auth, mac, sizeof(mac));
  return HB_NFC_OK;
}

static hb_nfc_err_t
authenticate(mfp_session_t *session, uint16_t address, const uint8_t *key, bool is_first) {
  if (session == NULL || key == NULL || !session->dep_ready)
    return HB_NFC_ERR_PARAM;
  if (!is_first && !session->authenticated)
    return HB_NFC_ERR_AUTH;
  clear_auth(session);
  uint8_t cmd[] = {is_first ? MFP_AUTH_FIRST : MFP_AUTH_NONFIRST,
                   (uint8_t)address,
                   (uint8_t)(address >> MFP_BYTE_SHIFT),
                   0};
  uint8_t rx[MFP_AUTH_REPLY_SIZE];
  int len = mfp_apdu_transceive(
      session, cmd, is_first ? sizeof(cmd) : sizeof(cmd) - 1, rx, sizeof(rx), MFP_TIMEOUT_MS);
  if (len == 1 && rx[0] != MFP_STATUS_OK)
    return HB_NFC_ERR_AUTH;
  if (len != 1 + MFP_BLOCK_SIZE || rx[0] != MFP_STATUS_OK)
    return HB_NFC_ERR_PROTOCOL;
  uint8_t iv[MFP_BLOCK_SIZE] = {0}, rnd_a[MFP_BLOCK_SIZE], rnd_b[MFP_BLOCK_SIZE];
  nfc_cbc_params_t cbc = {.encrypt = false, .key = key, .iv_in = iv, .iv_out = NULL};
  if (!nfc_aes_cbc_crypt(&cbc, rx + 1, MFP_BLOCK_SIZE, rnd_b))
    return HB_NFC_ERR_INTERNAL;
  esp_fill_random(rnd_a, sizeof(rnd_a));
  uint8_t token[MFP_AUTH_TOKEN_SIZE], next[1 + MFP_AUTH_TOKEN_SIZE];
  memcpy(token, rnd_a, sizeof(rnd_a));
  rotate_left(token + MFP_BLOCK_SIZE, rnd_b);
  next[0] = MFP_AUTH_CONTINUE;
  cbc.encrypt = true;
  if (!nfc_aes_cbc_crypt(&cbc, token, sizeof(token), next + 1))
    return HB_NFC_ERR_INTERNAL;
  len = mfp_apdu_transceive(session, next, sizeof(next), rx, sizeof(rx), MFP_TIMEOUT_MS);
  size_t reply_len = is_first ? MFP_AUTH_TOKEN_SIZE : MFP_BLOCK_SIZE;
  if (len == 1 && rx[0] != MFP_STATUS_OK)
    return HB_NFC_ERR_AUTH;
  if (len != (int)(1 + reply_len) || rx[0] != MFP_STATUS_OK)
    return HB_NFC_ERR_PROTOCOL;
  cbc.encrypt = false;
  if (!nfc_aes_cbc_crypt(&cbc, rx + 1, reply_len, token))
    return HB_NFC_ERR_INTERNAL;
  uint8_t rotated[MFP_BLOCK_SIZE];
  rotate_left(rotated, rnd_a);
  if (nfc_memcmp_ct(rotated, token + (is_first ? MFP_TI_SIZE : 0), sizeof(rotated)) != 0)
    return HB_NFC_ERR_AUTH;
  hb_nfc_err_t err =
      mfp_sl3_derive_session_keys(key, rnd_a, rnd_b, session->ses_enc, session->ses_auth);
  if (err != HB_NFC_OK)
    return fail_session(session, err);
  if (is_first) {
    memcpy(session->ti, token, MFP_TI_SIZE);
    session->read_counter = 0;
    session->write_counter = 0;
  }
  session->authenticated = true;
  return HB_NFC_OK;
}

hb_nfc_err_t mfp_sl3_auth_first(mfp_session_t *session, uint16_t address, const uint8_t key[16]) {
  return authenticate(session, address, key, true);
}

hb_nfc_err_t
mfp_sl3_auth_nonfirst(mfp_session_t *session, uint16_t address, const uint8_t key[16]) {
  return authenticate(session, address, key, false);
}

int mfp_sl3_compute_mac(const mfp_session_t *session,
                        const uint8_t *data,
                        size_t data_len,
                        uint8_t mac8[8]) {
  if (session == NULL || !session->authenticated || mac8 == NULL || (data == NULL && data_len))
    return -1;
  uint8_t full[MFP_BLOCK_SIZE];
  if (nfc_aes_cmac(session->ses_auth, MFP_BLOCK_SIZE, data, data_len, full) != 0)
    return -1;
  for (size_t i = 0; i < MFP_MAC_SIZE; i++)
    mac8[i] = full[2 * i + 1];
  return 0;
}

static bool command_mac(const mfp_session_t *session,
                        uint8_t opcode,
                        uint16_t counter,
                        const uint8_t *payload,
                        size_t len,
                        uint8_t *out_mac) {
  uint8_t data[MFP_MAC_HEADER + MFP_MAX_READ_BLOCKS * MFP_BLOCK_SIZE + 3];
  if (len > sizeof(data) - MFP_MAC_HEADER || (payload == NULL && len))
    return false;
  data[0] = opcode;
  data[1] = (uint8_t)counter;
  data[2] = (uint8_t)(counter >> MFP_BYTE_SHIFT);
  memcpy(data + 3, session->ti, MFP_TI_SIZE);
  if (len)
    memcpy(data + MFP_MAC_HEADER, payload, len);
  return mfp_sl3_compute_mac(session, data, MFP_MAC_HEADER + len, out_mac) == 0;
}

static void
build_iv(const mfp_session_t *session, uint16_t read_counter, bool is_read, uint8_t *out_iv) {
  uint8_t word[MFP_TI_SIZE] = {(uint8_t)read_counter,
                               (uint8_t)(read_counter >> MFP_BYTE_SHIFT),
                               (uint8_t)session->write_counter,
                               (uint8_t)(session->write_counter >> MFP_BYTE_SHIFT)};
  memcpy(out_iv + (is_read ? MFP_COUNTER_REPEAT * MFP_TI_SIZE : 0), session->ti, MFP_TI_SIZE);
  for (size_t i = 0; i < MFP_COUNTER_REPEAT; i++)
    memcpy(out_iv + (is_read ? 0 : MFP_TI_SIZE) + i * MFP_TI_SIZE, word, sizeof(word));
}

hb_nfc_err_t mfp_sl3_read(mfp_session_t *session,
                          uint16_t address,
                          uint8_t blocks,
                          bool is_encrypted,
                          uint8_t *out,
                          size_t capacity,
                          size_t *out_len) {
  if (out_len != NULL)
    *out_len = 0;
  size_t data_len = (size_t)blocks * MFP_BLOCK_SIZE;
  if (session == NULL || out == NULL || out_len == NULL || blocks == 0 ||
      blocks > MFP_MAX_READ_BLOCKS || data_len > capacity || address > UINT16_MAX - blocks + 1U)
    return HB_NFC_ERR_PARAM;
  if (!session->authenticated)
    return HB_NFC_ERR_AUTH;
  if (session->read_counter == UINT16_MAX)
    return fail_session(session, HB_NFC_ERR_AUTH);
  uint8_t cmd[4 + MFP_MAC_SIZE] = {is_encrypted ? MFP_READ_ENCRYPTED : MFP_READ_PLAIN,
                                   (uint8_t)address,
                                   (uint8_t)(address >> MFP_BYTE_SHIFT),
                                   blocks};
  if (!command_mac(session, cmd[0], session->read_counter, cmd + 1, 3, cmd + 4))
    return fail_session(session, HB_NFC_ERR_INTERNAL);
  uint8_t response[1 + MFP_MAX_READ_BLOCKS * MFP_BLOCK_SIZE + MFP_MAC_SIZE];
  int len =
      mfp_apdu_transceive(session, cmd, sizeof(cmd), response, sizeof(response), MFP_TIMEOUT_MS);
  if (len != (int)(1 + data_len + MFP_MAC_SIZE) || response[0] != MFP_STATUS_OK)
    return fail_session(session, HB_NFC_ERR_PROTOCOL);
  uint16_t next_counter = session->read_counter + 1;
  uint8_t mac_data[3 + MFP_MAX_READ_BLOCKS * MFP_BLOCK_SIZE], mac[MFP_MAC_SIZE];
  memcpy(mac_data, cmd + 1, 3);
  memcpy(mac_data + 3, response + 1, data_len);
  if (!command_mac(session, MFP_STATUS_OK, next_counter, mac_data, 3 + data_len, mac))
    return fail_session(session, HB_NFC_ERR_INTERNAL);
  if (nfc_memcmp_ct(mac, response + 1 + data_len, MFP_MAC_SIZE) != 0)
    return fail_session(session, HB_NFC_ERR_AUTH);
  if (is_encrypted) {
    uint8_t iv[MFP_BLOCK_SIZE];
    build_iv(session, next_counter, true, iv);
    nfc_cbc_params_t cbc = {.encrypt = false, .key = session->ses_enc, .iv_in = iv, .iv_out = NULL};
    if (!nfc_aes_cbc_crypt(&cbc, response + 1, data_len, out))
      return fail_session(session, HB_NFC_ERR_INTERNAL);
  } else {
    memcpy(out, response + 1, data_len);
  }
  session->read_counter = next_counter;
  *out_len = data_len;
  return HB_NFC_OK;
}

hb_nfc_err_t mfp_sl3_write(mfp_session_t *session,
                           uint16_t address,
                           bool is_encrypted,
                           const uint8_t *data,
                           size_t data_len) {
  if (session == NULL || data == NULL || data_len != MFP_BLOCK_SIZE ||
      (address >= MFP_KEY_BASE && address < MFP_CONFIG_BASE && !is_encrypted))
    return HB_NFC_ERR_PARAM;
  if (!session->authenticated)
    return HB_NFC_ERR_AUTH;
  if (session->write_counter == UINT16_MAX)
    return fail_session(session, HB_NFC_ERR_AUTH);
  uint8_t cmd[3 + MFP_BLOCK_SIZE + MFP_MAC_SIZE] = {is_encrypted ? MFP_WRITE_ENCRYPTED
                                                                 : MFP_WRITE_PLAIN,
                                                    (uint8_t)address,
                                                    (uint8_t)(address >> MFP_BYTE_SHIFT)};
  if (is_encrypted) {
    uint8_t iv[MFP_BLOCK_SIZE];
    build_iv(session, session->read_counter, false, iv);
    nfc_cbc_params_t cbc = {.encrypt = true, .key = session->ses_enc, .iv_in = iv, .iv_out = NULL};
    if (!nfc_aes_cbc_crypt(&cbc, data, data_len, cmd + 3))
      return fail_session(session, HB_NFC_ERR_INTERNAL);
  } else {
    memcpy(cmd + 3, data, data_len);
  }
  if (!command_mac(
          session, cmd[0], session->write_counter, cmd + 1, 2 + data_len, cmd + 3 + data_len))
    return fail_session(session, HB_NFC_ERR_INTERNAL);
  uint8_t response[1 + MFP_MAC_SIZE], mac[MFP_MAC_SIZE];
  int len =
      mfp_apdu_transceive(session, cmd, sizeof(cmd), response, sizeof(response), MFP_TIMEOUT_MS);
  if (len != sizeof(response) || response[0] != MFP_STATUS_OK)
    return fail_session(session, HB_NFC_ERR_PROTOCOL);
  uint16_t next_counter = session->write_counter + 1;
  if (!command_mac(session, MFP_STATUS_OK, next_counter, NULL, 0, mac))
    return fail_session(session, HB_NFC_ERR_INTERNAL);
  if (nfc_memcmp_ct(mac, response + 1, sizeof(mac)) != 0)
    return fail_session(session, HB_NFC_ERR_AUTH);
  session->write_counter = next_counter;
  return HB_NFC_OK;
}

size_t mfp_sl3_encrypt(
    mfp_session_t *session, const uint8_t *plain, size_t plain_len, uint8_t *out, size_t out_max) {
  if (session == NULL || !session->authenticated || out == NULL)
    return 0;
  size_t padded_len = nfc_iso9797_pad_method1(plain, plain_len, out, out_max);
  if (padded_len == 0)
    return 0;

  uint8_t iv_in[MFP_BLOCK_SIZE];
  memcpy(iv_in, session->iv_enc, MFP_BLOCK_SIZE);
  nfc_cbc_params_t cbc = {
      .encrypt = true, .key = session->ses_enc, .iv_in = iv_in, .iv_out = session->iv_enc};
  if (!nfc_aes_cbc_crypt(&cbc, out, padded_len, out)) {
    return 0;
  }
  return padded_len;
}

size_t mfp_sl3_decrypt(
    mfp_session_t *session, const uint8_t *enc, size_t enc_len, uint8_t *out, size_t out_max) {
  if (session == NULL || !session->authenticated || enc == NULL || out == NULL)
    return 0;
  if ((enc_len % MFP_BLOCK_SIZE) != 0 || enc_len > out_max)
    return 0;

  uint8_t iv_in[MFP_BLOCK_SIZE];
  memcpy(iv_in, session->iv_enc, MFP_BLOCK_SIZE);
  nfc_cbc_params_t cbc = {
      .encrypt = false, .key = session->ses_enc, .iv_in = iv_in, .iv_out = session->iv_enc};
  if (!nfc_aes_cbc_crypt(&cbc, enc, enc_len, out)) {
    return 0;
  }
  return enc_len;
}

#define MFP_GET_VERSION         0x60U
#define MFP_ADDITIONAL_FRAME    0xAFU
#define MFP_READ_SIGNATURE      0x3CU
#define MFP_STATUS_AUTH         0x06U
#define MFP_STATUS_UNAVAILABLE  0x0BU
#define MFP_WRAPPED_CLA         0x90U
#define MFP_WRAPPED_SW1         0x91U
#define MFP_VERSION_PART_SIZE   7U
#define MFP_WRAPPED_HEADER_SIZE 5U
#define MFP_WRAPPED_OVERHEAD    6U
#define MFP_SECTOR_LARGE_START  128U
#define MFP_SECTOR_LARGE_BASE   32U
#define MFP_SECTOR_SMALL_BLOCKS 4U
#define MFP_SECTOR_LARGE_BLOCKS 16U
#define MFP_DATA_BLOCK_COUNT    256U
#define MFP_CONFIG_COUNT        4U

hb_nfc_err_t mfp_get_version(mfp_session_t *session, uint8_t out[MFP_VERSION_SIZE]) {
  if (session == NULL || out == NULL)
    return HB_NFC_ERR_PARAM;
  uint8_t version[MFP_VERSION_SIZE], response[1 + 2 * MFP_VERSION_PART_SIZE];
  uint8_t command = MFP_GET_VERSION;
  size_t offset = 0;
  for (unsigned part = 0; part < 3U; part++) {
    size_t count = part == 2U ? 2U * MFP_VERSION_PART_SIZE : MFP_VERSION_PART_SIZE;
    int length =
        mfp_apdu_transceive(session, &command, 1, response, sizeof(response), MFP_TIMEOUT_MS);
    uint8_t status = part == 2U ? MFP_STATUS_OK : MFP_ADDITIONAL_FRAME;
    if (length != (int)(count + 1) || response[0] != status)
      return HB_NFC_ERR_PROTOCOL;
    memcpy(version + offset, response + 1, count);
    offset += count;
    command = MFP_ADDITIONAL_FRAME;
  }
  memcpy(out, version, sizeof(version));
  return HB_NFC_OK;
}

hb_nfc_err_t mfp_read_signature(mfp_session_t *session, uint8_t out[MFP_SIGNATURE_SIZE]) {
  if (session == NULL || out == NULL)
    return HB_NFC_ERR_PARAM;
  const uint8_t command[] = {MFP_READ_SIGNATURE, 0};
  uint8_t response[1 + MFP_SIGNATURE_SIZE];
  int length = mfp_apdu_transceive(
      session, command, sizeof(command), response, sizeof(response), MFP_TIMEOUT_MS);
  if (length != sizeof(response) || response[0] != MFP_STATUS_OK)
    return HB_NFC_ERR_PROTOCOL;
  memcpy(out, response + 1, MFP_SIGNATURE_SIZE);
  return HB_NFC_OK;
}

void mfp_listener_reset(mfp_listener_t *listener) {
  if (listener == NULL)
    return;
  mfp_listener_config_t config = listener->config;
  memset(listener, 0, sizeof(*listener));
  listener->config = config;
}

hb_nfc_err_t mfp_listener_init(mfp_listener_t *listener, const mfp_listener_config_t *config) {
  if (listener == NULL || config == NULL || config->key == NULL || config->read == NULL)
    return HB_NFC_ERR_PARAM;
  memset(listener, 0, sizeof(*listener));
  listener->config = *config;
  return HB_NFC_OK;
}

static uint8_t listener_auth(
    mfp_listener_t *listener, const uint8_t *req, size_t len, uint8_t *out, size_t *out_len) {
  bool is_first = req[0] == MFP_AUTH_FIRST;
  if (len != (is_first ? 4U : 3U) || (is_first && req[3] != 0) ||
      (!is_first && !listener->session.authenticated)) {
    mfp_listener_reset(listener);
    return MFP_STATUS_AUTH;
  }
  if (is_first)
    mfp_listener_reset(listener);
  listener->session.authenticated = false;
  listener->has_challenge = false;
  listener->key_address = (uint16_t)(req[1] | ((uint16_t)req[2] << MFP_BYTE_SHIFT));
  listener->has_auth_key =
      listener->config.key(listener->config.context, listener->key_address, listener->auth_key);
  if (!listener->has_auth_key)
    esp_fill_random(listener->auth_key, sizeof(listener->auth_key));
  esp_fill_random(listener->rnd_b, sizeof(listener->rnd_b));
  if (!nfc_aes_ecb_encrypt(listener->auth_key, listener->rnd_b, out)) {
    mfp_listener_reset(listener);
    return MFP_STATUS_AUTH;
  }
  listener->is_first = is_first;
  listener->has_challenge = true;
  *out_len = MFP_BLOCK_SIZE;
  return MFP_STATUS_OK;
}

static uint8_t listener_continue(
    mfp_listener_t *listener, const uint8_t *req, size_t len, uint8_t *out, size_t *out_len) {
  if (len != 1 + MFP_AUTH_TOKEN_SIZE || !listener->has_challenge)
    goto reject;
  listener->has_challenge = false;
  uint8_t iv[MFP_BLOCK_SIZE] = {0}, plain[MFP_AUTH_TOKEN_SIZE], rotated[MFP_BLOCK_SIZE];
  nfc_cbc_params_t cbc = {.encrypt = false, .key = listener->auth_key, .iv_in = iv, .iv_out = NULL};
  if (!nfc_aes_cbc_crypt(&cbc, req + 1, MFP_AUTH_TOKEN_SIZE, plain))
    goto reject;
  rotate_left(rotated, listener->rnd_b);
  if (!listener->has_auth_key ||
      nfc_memcmp_ct(rotated, plain + MFP_BLOCK_SIZE, MFP_BLOCK_SIZE) != 0)
    goto reject;
  mfp_session_t *session = &listener->session;
  if (mfp_sl3_derive_session_keys(
          listener->auth_key, plain, listener->rnd_b, session->ses_enc, session->ses_auth) !=
      HB_NFC_OK)
    goto reject;
  rotate_left(rotated, plain);
  memset(plain, 0, sizeof(plain));
  size_t reply_len = listener->is_first ? MFP_AUTH_TOKEN_SIZE : MFP_BLOCK_SIZE;
  if (listener->is_first) {
    esp_fill_random(session->ti, MFP_TI_SIZE);
    session->read_counter = 0;
    session->write_counter = 0;
    memcpy(plain, session->ti, MFP_TI_SIZE);
  }
  memcpy(plain + (listener->is_first ? MFP_TI_SIZE : 0), rotated, MFP_BLOCK_SIZE);
  cbc.encrypt = true;
  if (!nfc_aes_cbc_crypt(&cbc, plain, reply_len, out))
    goto reject;
  memset(listener->auth_key, 0, sizeof(listener->auth_key));
  memset(listener->rnd_b, 0, sizeof(listener->rnd_b));
  session->authenticated = true;
  *out_len = reply_len;
  return MFP_STATUS_OK;
reject:
  mfp_listener_reset(listener);
  return MFP_STATUS_AUTH;
}

static bool listener_can_read(const mfp_listener_t *listener, uint16_t address) {
  if (address >= MFP_CONFIG_BASE && address - MFP_CONFIG_BASE < MFP_CONFIG_COUNT)
    return true;
  if (address >= MFP_DATA_BLOCK_COUNT || listener->key_address < MFP_KEY_BASE)
    return false;
  uint16_t sector =
      address < MFP_SECTOR_LARGE_START
          ? address / MFP_SECTOR_SMALL_BLOCKS
          : MFP_SECTOR_LARGE_BASE + (address - MFP_SECTOR_LARGE_START) / MFP_SECTOR_LARGE_BLOCKS;
  return (listener->key_address - MFP_KEY_BASE) / 2U == sector;
}

static uint8_t listener_read(
    mfp_listener_t *listener, const uint8_t *req, size_t len, uint8_t *out, size_t *out_len) {
  mfp_session_t *session = &listener->session;
  if (!session->authenticated || len != 4 + MFP_MAC_SIZE || req[3] == 0 ||
      req[3] > MFP_MAX_READ_BLOCKS || session->read_counter == UINT16_MAX)
    goto reject;
  uint8_t mac[MFP_MAC_SIZE];
  if (!command_mac(session, req[0], session->read_counter, req + 1, 3, mac) ||
      nfc_memcmp_ct(mac, req + 4, MFP_MAC_SIZE) != 0)
    goto reject;
  uint16_t address = (uint16_t)(req[1] | ((uint16_t)req[2] << MFP_BYTE_SHIFT));
  size_t data_len = req[3] * MFP_BLOCK_SIZE;
  uint8_t block[MFP_MAX_READ_BLOCKS * MFP_BLOCK_SIZE];
  bool is_data = address < MFP_DATA_BLOCK_COUNT && req[3] <= MFP_DATA_BLOCK_COUNT - address;
  bool is_config = address >= MFP_CONFIG_BASE && address < MFP_CONFIG_BASE + MFP_CONFIG_COUNT &&
                   req[3] <= MFP_CONFIG_BASE + MFP_CONFIG_COUNT - address;
  if (!is_data && !is_config)
    return MFP_STATUS_UNAVAILABLE;
  for (uint16_t i = 0; i < req[3]; i++) {
    if (!listener_can_read(listener, address + i) ||
        !listener->config.read(listener->config.context,
                               listener->key_address,
                               address + i,
                               block + i * MFP_BLOCK_SIZE))
      return MFP_STATUS_UNAVAILABLE;
  }
  uint16_t counter = session->read_counter + 1;
  if (req[0] == MFP_READ_ENCRYPTED) {
    uint8_t iv[MFP_BLOCK_SIZE];
    build_iv(session, counter, true, iv);
    nfc_cbc_params_t cbc = {.encrypt = true, .key = session->ses_enc, .iv_in = iv, .iv_out = NULL};
    if (!nfc_aes_cbc_crypt(&cbc, block, data_len, out))
      goto reject;
  } else {
    memcpy(out, block, data_len);
  }
  uint8_t payload[3 + MFP_MAX_READ_BLOCKS * MFP_BLOCK_SIZE];
  memcpy(payload, req + 1, 3);
  memcpy(payload + 3, out, data_len);
  if (!command_mac(session, MFP_STATUS_OK, counter, payload, 3 + data_len, out + data_len))
    goto reject;
  session->read_counter = counter;
  *out_len = data_len + MFP_MAC_SIZE;
  return MFP_STATUS_OK;
reject:
  mfp_listener_reset(listener);
  return MFP_STATUS_AUTH;
}

static uint8_t listener_write(
    mfp_listener_t *listener, const uint8_t *req, size_t len, uint8_t *out, size_t *out_len) {
  mfp_session_t *session = &listener->session;
  if (!session->authenticated || len != 3 + MFP_BLOCK_SIZE + MFP_MAC_SIZE ||
      session->write_counter == UINT16_MAX)
    goto reject;
  uint8_t mac[MFP_MAC_SIZE];
  if (!command_mac(session, req[0], session->write_counter, req + 1, 2 + MFP_BLOCK_SIZE, mac) ||
      nfc_memcmp_ct(mac, req + 3 + MFP_BLOCK_SIZE, MFP_MAC_SIZE) != 0)
    goto reject;
  uint16_t address = (uint16_t)(req[1] | ((uint16_t)req[2] << MFP_BYTE_SHIFT));
  if ((address >= MFP_KEY_BASE && address < MFP_CONFIG_BASE && req[0] != MFP_WRITE_ENCRYPTED) ||
      listener->config.write == NULL)
    return MFP_STATUS_UNAVAILABLE;
  uint8_t block[MFP_BLOCK_SIZE];
  if (req[0] == MFP_WRITE_ENCRYPTED) {
    uint8_t iv[MFP_BLOCK_SIZE];
    build_iv(session, session->read_counter, false, iv);
    nfc_cbc_params_t cbc = {.encrypt = false, .key = session->ses_enc, .iv_in = iv, .iv_out = NULL};
    if (!nfc_aes_cbc_crypt(&cbc, req + 3, MFP_BLOCK_SIZE, block))
      goto reject;
  } else {
    memcpy(block, req + 3, sizeof(block));
  }
  uint16_t counter = session->write_counter + 1;
  if (!command_mac(session, MFP_STATUS_OK, counter, NULL, 0, out))
    goto reject;
  if (!listener->config.write(listener->config.context, listener->key_address, address, block))
    return MFP_STATUS_UNAVAILABLE;
  session->write_counter = counter;
  *out_len = MFP_MAC_SIZE;
  return MFP_STATUS_OK;
reject:
  mfp_listener_reset(listener);
  return MFP_STATUS_AUTH;
}

hb_nfc_err_t mfp_listener_exchange(mfp_listener_t *listener,
                                   const uint8_t *request,
                                   size_t length,
                                   uint8_t *out,
                                   size_t capacity,
                                   size_t *out_len) {
  if (out_len != NULL)
    *out_len = 0;
  if (listener == NULL || request == NULL || out == NULL || out_len == NULL || length == 0 ||
      capacity < MFP_LISTENER_RESPONSE_SIZE || listener->config.key == NULL ||
      listener->config.read == NULL)
    return HB_NFC_ERR_PARAM;
  bool is_wrapped = request[0] == MFP_WRAPPED_CLA;
  uint8_t native[1 + MFP_AUTH_TOKEN_SIZE];
  if (is_wrapped) {
    if (length < MFP_WRAPPED_HEADER_SIZE || request[2] || request[3])
      return HB_NFC_ERR_PROTOCOL;
    size_t lc = request[4];
    bool is_empty = length == MFP_WRAPPED_HEADER_SIZE && lc == 0;
    if (!is_empty && (length != MFP_WRAPPED_OVERHEAD + lc || request[length - 1] != 0))
      return HB_NFC_ERR_PROTOCOL;
    if (lc > sizeof(native) - 1)
      return HB_NFC_ERR_PROTOCOL;
    native[0] = request[1];
    if (lc)
      memcpy(native + 1, request + MFP_WRAPPED_HEADER_SIZE, lc);
    request = native;
    length = 1 + lc;
  }
  uint8_t status = MFP_STATUS_UNAVAILABLE;
  size_t payload_len = 0;
  uint8_t *payload = out + (is_wrapped ? 0 : 1);
  if (request[0] != MFP_ADDITIONAL_FRAME && request[0] != MFP_GET_VERSION)
    listener->version_stage = 0;
  switch (request[0]) {
    case MFP_AUTH_FIRST:
    case MFP_AUTH_NONFIRST:
      status = listener_auth(listener, request, length, payload, &payload_len);
      break;
    case MFP_AUTH_CONTINUE:
      status = listener_continue(listener, request, length, payload, &payload_len);
      break;
    case MFP_READ_ENCRYPTED:
    case MFP_READ_PLAIN:
      status = listener_read(listener, request, length, payload, &payload_len);
      break;
    case MFP_WRITE_ENCRYPTED:
    case MFP_WRITE_PLAIN:
      status = listener_write(listener, request, length, payload, &payload_len);
      break;
    case MFP_GET_VERSION:
      listener->version_stage = 0;
      if (length == 1 && listener->config.version != NULL) {
        memcpy(payload, listener->config.version, MFP_VERSION_PART_SIZE);
        payload_len = MFP_VERSION_PART_SIZE;
        listener->version_stage = 1;
        listener->is_version_wrapped = is_wrapped;
        status = MFP_ADDITIONAL_FRAME;
      }
      break;
    case MFP_ADDITIONAL_FRAME:
      if (length == 1 && listener->version_stage && listener->config.version != NULL &&
          listener->is_version_wrapped == is_wrapped) {
        payload_len =
            listener->version_stage == 1 ? MFP_VERSION_PART_SIZE : 2 * MFP_VERSION_PART_SIZE;
        memcpy(payload,
               listener->config.version + listener->version_stage * MFP_VERSION_PART_SIZE,
               payload_len);
        status = listener->version_stage == 1 ? MFP_ADDITIONAL_FRAME : MFP_STATUS_OK;
        listener->version_stage = listener->version_stage == 1 ? 2 : 0;
      } else {
        listener->version_stage = 0;
      }
      break;
    case MFP_READ_SIGNATURE:
      if (length == 2 && request[1] == 0 && listener->config.signature != NULL) {
        memcpy(payload, listener->config.signature, MFP_SIGNATURE_SIZE);
        payload_len = MFP_SIGNATURE_SIZE;
        status = MFP_STATUS_OK;
      }
      break;
    default:
      break;
  }
  if (is_wrapped) {
    out[payload_len] = MFP_WRAPPED_SW1;
    out[payload_len + 1] = status;
    *out_len = payload_len + 2;
  } else {
    out[0] = status;
    *out_len = payload_len + 1;
  }
  return HB_NFC_OK;
}
