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

#include "t2t_aes.h"

#include <string.h>

#include "esp_log.h"
#include "nfc_crypto.h"

#define AUTH_CONTINUE             0xAFU
#define AUTH_SUCCESS              0x00U
#define BYTE_BITS                 8U
#define VECTOR_SIZE               32U
#define VECTOR_PREFIX_SIZE        6U
#define VECTOR_RANDOM_PREFIX_SIZE 2U
#define VECTOR_XOR_SIZE           6U
#define VECTOR_RANDOM_SUFFIX_SIZE 8U
#define MAC_STRIDE                2U
#define COUNTER_SIZE              2U

static const char *TAG = "T2T_AES";

static bool
crypt(bool encrypt, const uint8_t *key, const uint8_t *data, size_t length, uint8_t *out_data) {
  const uint8_t iv[T2T_AES_BLOCK_SIZE] = {0};
  nfc_cbc_params_t params = {.encrypt = encrypt, .key = key, .iv_in = iv};
  return nfc_aes_cbc_crypt(&params, data, length, out_data);
}

static void rotate(const uint8_t *data, uint8_t *out_data) {
  memcpy(out_data, data + 1, T2T_AES_BLOCK_SIZE - 1);
  out_data[T2T_AES_BLOCK_SIZE - 1] = data[0];
}

static bool derive(const uint8_t *key, const uint8_t *a, const uint8_t *b, uint8_t *out_key) {
  uint8_t vector[VECTOR_SIZE] = {0x5A, 0xA5, 0x00, 0x01, 0x00, 0x80};
  size_t offset = VECTOR_PREFIX_SIZE;
  memcpy(vector + offset, a, VECTOR_RANDOM_PREFIX_SIZE);
  offset += VECTOR_RANDOM_PREFIX_SIZE;
  for (size_t i = 0; i < VECTOR_XOR_SIZE; i++)
    vector[offset++] = a[VECTOR_RANDOM_PREFIX_SIZE + i] ^ b[i];
  memcpy(vector + offset, b + VECTOR_XOR_SIZE, T2T_AES_BLOCK_SIZE - VECTOR_XOR_SIZE);
  offset += T2T_AES_BLOCK_SIZE - VECTOR_XOR_SIZE;
  memcpy(vector + offset, a + VECTOR_RANDOM_SUFFIX_SIZE, VECTOR_RANDOM_SUFFIX_SIZE);
  return nfc_aes_cmac(key, T2T_AES_BLOCK_SIZE, vector, sizeof(vector), out_key) == 0;
}

static bool
mac(const t2t_aes_session_t *session, const uint8_t *data, size_t length, uint8_t *out_mac) {
  uint8_t input[COUNTER_SIZE + T2T_AES_DATA_MAX];
  uint8_t full[T2T_AES_BLOCK_SIZE];
  input[0] = (uint8_t)session->counter;
  input[1] = (uint8_t)(session->counter >> BYTE_BITS);
  if (length)
    memcpy(input + COUNTER_SIZE, data, length);
  if (nfc_aes_cmac(session->mac_key, sizeof(session->mac_key), input, length + COUNTER_SIZE, full))
    return false;
  for (size_t i = 0; i < T2T_AES_MAC_SIZE; i++)
    out_mac[i] = full[MAC_STRIDE * i + 1];
  return true;
}

void t2t_aes_reset(t2t_aes_session_t *session) {
  if (session == NULL)
    return;
  volatile uint8_t *bytes = (volatile uint8_t *)session;
  for (size_t i = 0; i < sizeof(*session); i++)
    bytes[i] = 0;
}

esp_err_t t2t_aes_begin(t2t_aes_session_t *session,
                        const uint8_t key[T2T_AES_BLOCK_SIZE],
                        const uint8_t nonce[T2T_AES_BLOCK_SIZE],
                        uint8_t out_reply[T2T_AES_AUTH_REPLY_SIZE]) {
  if (session == NULL)
    return ESP_ERR_INVALID_ARG;
  t2t_aes_reset(session);
  if (key == NULL || nonce == NULL || out_reply == NULL)
    return ESP_ERR_INVALID_ARG;
  memcpy(session->key, key, sizeof(session->key));
  memcpy(session->nonce, nonce, sizeof(session->nonce));
  if (!crypt(true, key, nonce, T2T_AES_BLOCK_SIZE, out_reply + 1)) {
    t2t_aes_reset(session);
    ESP_LOGE(TAG, "AES challenge encryption failed");
    return ESP_FAIL;
  }
  out_reply[0] = AUTH_CONTINUE;
  session->is_pending = true;
  return ESP_OK;
}

esp_err_t t2t_aes_finish(t2t_aes_session_t *session,
                         const uint8_t *frame,
                         size_t length,
                         bool secure,
                         uint8_t out_reply[T2T_AES_AUTH_REPLY_SIZE]) {
  if (session == NULL)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  uint8_t plain[2 * T2T_AES_BLOCK_SIZE], rotated[T2T_AES_BLOCK_SIZE];
  if (!session->is_pending || frame == NULL || out_reply == NULL ||
      length != T2T_AES_AUTH_CONTINUE_SIZE || frame[0] != AUTH_CONTINUE)
    goto cleanup;
  if (!crypt(false, session->key, frame + 1, sizeof(plain), plain))
    goto cleanup;
  rotate(session->nonce, rotated);
  if (nfc_memcmp_ct(rotated, plain + T2T_AES_BLOCK_SIZE, sizeof(rotated)))
    goto cleanup;
  if (secure && !derive(session->key, plain, session->nonce, session->mac_key))
    goto cleanup;
  rotate(plain, rotated);
  if (!crypt(true, session->key, rotated, sizeof(rotated), out_reply + 1))
    goto cleanup;
  out_reply[0] = AUTH_SUCCESS;
  session->is_pending = false;
  session->is_authenticated = true;
  session->is_secure = secure;
  session->counter = 0;
  memset(session->key, 0, sizeof(session->key));
  memset(session->nonce, 0, sizeof(session->nonce));
  error = ESP_OK;
cleanup:
  memset(plain, 0, sizeof(plain));
  memset(rotated, 0, sizeof(rotated));
  if (error != ESP_OK)
    t2t_aes_reset(session);
  return error;
}

esp_err_t t2t_aes_verify(t2t_aes_session_t *session,
                         const uint8_t *frame,
                         size_t length,
                         size_t *out_length) {
  if (out_length)
    *out_length = 0;
  if (session == NULL)
    return ESP_ERR_INVALID_ARG;
  uint8_t expected[T2T_AES_MAC_SIZE];
  if (!session->is_authenticated || !session->is_secure || frame == NULL || out_length == NULL ||
      length < T2T_AES_MAC_SIZE || length > T2T_AES_DATA_MAX + T2T_AES_MAC_SIZE ||
      session->counter == UINT16_MAX || !mac(session, frame, length - T2T_AES_MAC_SIZE, expected) ||
      nfc_memcmp_ct(expected, frame + length - T2T_AES_MAC_SIZE, sizeof(expected))) {
    t2t_aes_reset(session);
    return ESP_ERR_INVALID_RESPONSE;
  }
  session->counter++;
  *out_length = length - T2T_AES_MAC_SIZE;
  return ESP_OK;
}

esp_err_t t2t_aes_sign(t2t_aes_session_t *session,
                       const uint8_t *data,
                       size_t length,
                       uint8_t out_mac[T2T_AES_MAC_SIZE]) {
  if (session == NULL)
    return ESP_ERR_INVALID_ARG;
  if (!session->is_authenticated || !session->is_secure || out_mac == NULL ||
      (data == NULL && length != 0) || length > T2T_AES_DATA_MAX ||
      !mac(session, data, length, out_mac)) {
    t2t_aes_reset(session);
    return ESP_ERR_INVALID_RESPONSE;
  }
  if (session->counter != UINT16_MAX)
    session->counter++;
  return ESP_OK;
}

esp_err_t t2t_aes_reader_challenge(t2t_aes_session_t *session,
                                   const uint8_t *key,
                                   const uint8_t *nonce,
                                   const uint8_t *reply,
                                   size_t length,
                                   bool secure,
                                   uint8_t *out_frame) {
  if (!session)
    return ESP_ERR_INVALID_ARG;
  t2t_aes_reset(session);
  uint8_t b[T2T_AES_BLOCK_SIZE] = {0}, plain[2 * T2T_AES_BLOCK_SIZE] = {0};
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  if (!key || !nonce || !reply || !out_frame || length != T2T_AES_AUTH_REPLY_SIZE ||
      reply[0] != AUTH_CONTINUE)
    goto cleanup;
  if (!crypt(false, key, reply + 1, sizeof(b), b))
    goto cleanup;
  memcpy(plain, nonce, T2T_AES_BLOCK_SIZE);
  rotate(b, plain + T2T_AES_BLOCK_SIZE);
  if (!crypt(true, key, plain, sizeof(plain), out_frame + 1) ||
      (secure && !derive(key, nonce, b, session->mac_key)))
    goto cleanup;
  memcpy(session->key, key, T2T_AES_BLOCK_SIZE);
  memcpy(session->nonce, nonce, T2T_AES_BLOCK_SIZE);
  session->is_pending = true;
  session->is_secure = secure;
  out_frame[0] = AUTH_CONTINUE;
  error = ESP_OK;
cleanup:
  memset(b, 0, sizeof(b));
  memset(plain, 0, sizeof(plain));
  if (error != ESP_OK)
    t2t_aes_reset(session);
  return error;
}

esp_err_t t2t_aes_reader_complete(t2t_aes_session_t *session, const uint8_t *reply, size_t length) {
  if (!session)
    return ESP_ERR_INVALID_ARG;
  uint8_t plain[T2T_AES_BLOCK_SIZE] = {0}, expected[T2T_AES_BLOCK_SIZE] = {0};
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  if (!session->is_pending || !reply || length != T2T_AES_AUTH_REPLY_SIZE ||
      reply[0] != AUTH_SUCCESS || !crypt(false, session->key, reply + 1, sizeof(plain), plain))
    goto cleanup;
  rotate(session->nonce, expected);
  if (nfc_memcmp_ct(plain, expected, sizeof(plain)))
    goto cleanup;
  memset(session->key, 0, sizeof(session->key));
  memset(session->nonce, 0, sizeof(session->nonce));
  session->is_pending = false;
  session->is_authenticated = true;
  error = ESP_OK;
cleanup:
  memset(plain, 0, sizeof(plain));
  memset(expected, 0, sizeof(expected));
  if (error != ESP_OK)
    t2t_aes_reset(session);
  return error;
}
