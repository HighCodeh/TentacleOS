// SPDX-License-Identifier: GPL-3.0-or-later
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

#include "nfc_crypto.h"

#include "firmware_crypto.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "legacy_des/des.h"

static const char *TAG = "NFC_CRYPTO";

#define AES_BLOCK_SIZE    16U
#define AES_KEY_SIZE_BITS 128
#define DES_BLOCK_SIZE    8U
#define TDES_2KEY_LEN     16U
#define TDES_3KEY_LEN     24U

#define AES_CMAC_RB   0x87U
#define DES_CMAC_RB   0x1BU
#define CMAC_MSB_MASK 0x80U

#define ISO9797_PAD_BYTE 0x80U

#define EV2_SV_MARKER_A     0xA5U
#define EV2_SV_MARKER_B     0x5AU
#define EV2_SV_COUNTER      0x01U
#define EV2_SV_KEY_LEN_BITS 0x80U

#define EV2_MAC_HEADER_SIZE 7U
#define EV2_TI_SIZE         4U

#define DIVERSIFY_AES_MAX_INPUT   31U
#define DIVERSIFY_2TDEA_MAX_INPUT 15U
#define DIVERSIFY_SV_CONST_1      0x01U
#define DIVERSIFY_SV_CONST_2      0x02U
#define DIVERSIFY_PARITY_MASK     0xFEU
#define DIVERSIFY_PARITY_BIT      0x01U
#define DIVERSIFY_UID_LEN         7U
#define DIVERSIFY_AID_LEN         3U

int nfc_memcmp_ct(const void *a, const void *b, size_t n) {
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++)
    diff |= (uint8_t)(pa[i] ^ pb[i]);
  return (int)diff;
}

bool nfc_aes_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  firmware_crypto_cipher_t params = {
      .key = {key, AES_BLOCK_SIZE}, .algorithm = PSA_ALG_ECB_NO_PADDING, .is_encrypt = true};
  return firmware_crypto_cipher(&params, (firmware_crypto_span_t){in, AES_BLOCK_SIZE}, out) ==
         ESP_OK;
}

bool nfc_aes_cbc_crypt(const nfc_cbc_params_t *p, const uint8_t *in, size_t len, uint8_t *out) {
  if (p == NULL || p->key == NULL || p->iv_in == NULL || in == NULL || out == NULL ||
      len % AES_BLOCK_SIZE != 0)
    return false;
  uint8_t next_iv[AES_BLOCK_SIZE];
  memcpy(next_iv, p->iv_in, AES_BLOCK_SIZE);
  if (!p->encrypt && len != 0)
    memcpy(next_iv, in + len - AES_BLOCK_SIZE, AES_BLOCK_SIZE);
  firmware_crypto_cipher_t params = {.key = {p->key, AES_BLOCK_SIZE},
                                     .iv = {p->iv_in, AES_BLOCK_SIZE},
                                     .algorithm = PSA_ALG_CBC_NO_PADDING,
                                     .is_encrypt = p->encrypt};
  if (firmware_crypto_cipher(&params, (firmware_crypto_span_t){in, len}, out) != ESP_OK)
    return false;
  if (p->encrypt && len != 0)
    memcpy(next_iv, out + len - AES_BLOCK_SIZE, AES_BLOCK_SIZE);
  if (p->iv_out != NULL)
    memcpy(p->iv_out, next_iv, AES_BLOCK_SIZE);
  return true;
}

int nfc_aes_cmac(
    const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len, uint8_t tag[16]) {
  if (key_len != AES_BLOCK_SIZE)
    return -1;
  return firmware_crypto_mac((firmware_crypto_span_t){key, key_len},
                             (firmware_crypto_span_t){msg, msg_len},
                             PSA_ALG_CMAC,
                             tag) == ESP_OK
             ? 0
             : -1;
}

static bool
tdes_ecb_encrypt(const uint8_t *key, size_t key_len, const uint8_t in[8], uint8_t out[8]) {
  if (key == NULL || in == NULL || out == NULL)
    return false;
  if (key_len != TDES_2KEY_LEN && key_len != TDES_3KEY_LEN)
    return false;

  mbedtls_des3_context ctx;
  mbedtls_des3_init(&ctx);
  int rc = (key_len == TDES_2KEY_LEN) ? mbedtls_des3_set2key_enc(&ctx, key)
                                      : mbedtls_des3_set3key_enc(&ctx, key);
  if (rc != 0) {
    mbedtls_des3_free(&ctx);
    return false;
  }
  rc = mbedtls_des3_crypt_ecb(&ctx, in, out);
  mbedtls_des3_free(&ctx);
  return rc == 0;
}

static void leftshift_onebit_64(const uint8_t *in, uint8_t *out) {
  for (int i = 0; i < (int)(DES_BLOCK_SIZE - 1); i++) {
    out[i] = (uint8_t)((in[i] << 1) | (in[i + 1] >> 7));
  }
  out[DES_BLOCK_SIZE - 1] = (uint8_t)(in[DES_BLOCK_SIZE - 1] << 1);
}

static int tdes_cmac_subkeys(const uint8_t *key, size_t key_len, uint8_t *k1, uint8_t *k2) {
  uint8_t L[DES_BLOCK_SIZE] = {0};
  if (!tdes_ecb_encrypt(key, key_len, L, L))
    return -1;

  leftshift_onebit_64(L, k1);
  if (L[0] & CMAC_MSB_MASK)
    k1[DES_BLOCK_SIZE - 1] ^= DES_CMAC_RB;

  leftshift_onebit_64(k1, k2);
  if (k1[0] & CMAC_MSB_MASK)
    k2[DES_BLOCK_SIZE - 1] ^= DES_CMAC_RB;
  return 0;
}

static int nfc_tdes_cmac(
    const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len, uint8_t tag[8]) {
  if (key == NULL || msg == NULL || tag == NULL)
    return -1;
  if (key_len != TDES_2KEY_LEN && key_len != TDES_3KEY_LEN)
    return -1;

  uint8_t k1[DES_BLOCK_SIZE], k2[DES_BLOCK_SIZE];
  if (tdes_cmac_subkeys(key, key_len, k1, k2) != 0)
    return -1;

  int n = (msg_len == 0) ? 1 : (int)((msg_len + DES_BLOCK_SIZE - 1) / DES_BLOCK_SIZE);
  bool padded = (msg_len == 0) || (msg_len % DES_BLOCK_SIZE != 0);

  uint8_t X[DES_BLOCK_SIZE] = {0};
  uint8_t Y[DES_BLOCK_SIZE];

  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < (int)DES_BLOCK_SIZE; j++)
      Y[j] = msg[i * DES_BLOCK_SIZE + j] ^ X[j];
    if (!tdes_ecb_encrypt(key, key_len, Y, X))
      return -1;
  }

  uint8_t last[DES_BLOCK_SIZE] = {0};
  size_t last_len = (msg_len == 0) ? 0 : (msg_len - (size_t)(n - 1) * DES_BLOCK_SIZE);
  if (last_len > 0)
    memcpy(last, msg + (size_t)(n - 1) * DES_BLOCK_SIZE, last_len);
  if (padded)
    last[last_len] = ISO9797_PAD_BYTE;

  const uint8_t *subkey = padded ? k2 : k1;
  for (int j = 0; j < (int)DES_BLOCK_SIZE; j++)
    Y[j] = (uint8_t)(last[j] ^ subkey[j] ^ X[j]);
  if (!tdes_ecb_encrypt(key, key_len, Y, tag))
    return -1;
  return 0;
}

int nfc_tdes_cbc_mac(
    const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t mac_out[8]) {
  if (key == NULL || data == NULL || mac_out == NULL)
    return -1;
  if (key_len != TDES_2KEY_LEN && key_len != TDES_3KEY_LEN)
    return -1;

  mbedtls_des3_context ctx;
  mbedtls_des3_init(&ctx);
  int rc = (key_len == TDES_2KEY_LEN) ? mbedtls_des3_set2key_enc(&ctx, key)
                                      : mbedtls_des3_set3key_enc(&ctx, key);
  if (rc != 0) {
    mbedtls_des3_free(&ctx);
    return -1;
  }

  uint8_t iv[DES_BLOCK_SIZE] = {0};
  size_t n_blocks = (data_len + DES_BLOCK_SIZE - 1) / DES_BLOCK_SIZE;
  if (n_blocks == 0)
    n_blocks = 1;

  for (size_t i = 0; i < n_blocks; i++) {
    uint8_t block[DES_BLOCK_SIZE] = {0};
    size_t copy = (i * DES_BLOCK_SIZE + DES_BLOCK_SIZE <= data_len)
                      ? DES_BLOCK_SIZE
                      : (data_len - i * DES_BLOCK_SIZE);
    if (copy > 0)
      memcpy(block, data + i * DES_BLOCK_SIZE, copy);

    uint8_t out[DES_BLOCK_SIZE];
    rc = mbedtls_des3_crypt_cbc(&ctx, MBEDTLS_DES_ENCRYPT, DES_BLOCK_SIZE, iv, block, out);
    if (rc != 0) {
      mbedtls_des3_free(&ctx);
      return -1;
    }
  }

  memcpy(mac_out, iv, DES_BLOCK_SIZE);
  mbedtls_des3_free(&ctx);
  return 0;
}

int nfc_mac_compute(const nfc_mac_input_t *in, uint8_t *mac_out, size_t *mac_len_out) {
  if (in == NULL || in->key == NULL || in->data == NULL || mac_out == NULL || mac_len_out == NULL)
    return -1;

  uint8_t full[AES_BLOCK_SIZE];
  switch (in->type) {
    case NFC_MAC_AES_CMAC_16:
      if (nfc_aes_cmac(in->key, in->key_len, in->data, in->data_len, full) != 0)
        return -1;
      memcpy(mac_out, full, AES_BLOCK_SIZE);
      *mac_len_out = AES_BLOCK_SIZE;
      break;

    case NFC_MAC_AES_CMAC_EV1_8:
      if (nfc_aes_cmac(in->key, in->key_len, in->data, in->data_len, full) != 0)
        return -1;
      memcpy(mac_out, full, DES_BLOCK_SIZE);
      *mac_len_out = DES_BLOCK_SIZE;
      break;

    case NFC_MAC_AES_CMAC_EV2_8:
      if (nfc_aes_cmac(in->key, in->key_len, in->data, in->data_len, full) != 0)
        return -1;
      for (int i = 0; i < (int)DES_BLOCK_SIZE; i++)
        mac_out[i] = full[1 + 2 * i];
      *mac_len_out = DES_BLOCK_SIZE;
      break;

    case NFC_MAC_3DES_CBC_8:
      if (nfc_tdes_cbc_mac(in->key, in->key_len, in->data, in->data_len, full) != 0)
        return -1;
      memcpy(mac_out, full, DES_BLOCK_SIZE);
      *mac_len_out = DES_BLOCK_SIZE;
      break;

    case NFC_MAC_3DES_CBC_4:
      if (nfc_tdes_cbc_mac(in->key, in->key_len, in->data, in->data_len, full) != 0)
        return -1;
      memcpy(mac_out, full, 4);
      *mac_len_out = 4;
      break;

    default:
      return -1;
  }
  return 0;
}

int nfc_mac_verify(const nfc_mac_input_t *in, const uint8_t *mac_rx, size_t mac_rx_len) {
  uint8_t mac_calc[AES_BLOCK_SIZE];
  size_t mac_calc_len = 0;
  if (in == NULL || mac_rx == NULL)
    return -1;
  if (nfc_mac_compute(in, mac_calc, &mac_calc_len) != 0)
    return -1;
  if (mac_calc_len != mac_rx_len)
    return -2;
  return (nfc_memcmp_ct(mac_calc, mac_rx, mac_calc_len) == 0) ? 0 : -3;
}

void nfc_ev2_derive_session_keys(const uint8_t *kx,
                                 const uint8_t *rnd_a,
                                 const uint8_t *rnd_b,
                                 uint8_t *ses_mac,
                                 uint8_t *ses_enc) {
  uint8_t ctx[26];
  uint8_t sv[32];

  ctx[0] = rnd_a[14];
  ctx[1] = rnd_a[15];
  ctx[2] = (uint8_t)(rnd_a[13] ^ rnd_b[10]);
  ctx[3] = (uint8_t)(rnd_a[12] ^ rnd_b[11]);
  ctx[4] = (uint8_t)(rnd_a[11] ^ rnd_b[12]);
  ctx[5] = (uint8_t)(rnd_a[10] ^ rnd_b[13]);
  ctx[6] = (uint8_t)(rnd_a[9] ^ rnd_b[14]);
  ctx[7] = (uint8_t)(rnd_a[8] ^ rnd_b[15]);
  ctx[8] = rnd_b[9];
  ctx[9] = rnd_b[8];
  ctx[10] = rnd_b[7];
  ctx[11] = rnd_b[6];
  ctx[12] = rnd_b[5];
  ctx[13] = rnd_b[4];
  ctx[14] = rnd_b[3];
  ctx[15] = rnd_b[2];
  ctx[16] = rnd_b[1];
  ctx[17] = rnd_b[0];
  ctx[18] = rnd_a[7];
  ctx[19] = rnd_a[6];
  ctx[20] = rnd_a[5];
  ctx[21] = rnd_a[4];
  ctx[22] = rnd_a[3];
  ctx[23] = rnd_a[2];
  ctx[24] = rnd_a[1];
  ctx[25] = rnd_a[0];

  sv[0] = EV2_SV_MARKER_A;
  sv[1] = EV2_SV_MARKER_B;
  sv[2] = 0x00;
  sv[3] = EV2_SV_COUNTER;
  sv[4] = 0x00;
  sv[5] = EV2_SV_KEY_LEN_BITS;
  memcpy(sv + 6, ctx, 26);
  (void)nfc_aes_cmac(kx, AES_BLOCK_SIZE, sv, sizeof(sv), ses_mac);

  sv[0] = EV2_SV_MARKER_B;
  sv[1] = EV2_SV_MARKER_A;
  (void)nfc_aes_cmac(kx, AES_BLOCK_SIZE, sv, sizeof(sv), ses_enc);
}

void nfc_ev2_mac_truncate(const uint8_t cmac16[16], uint8_t mac8[8]) {
  for (int i = 0; i < (int)DES_BLOCK_SIZE; i++)
    mac8[i] = cmac16[1 + 2 * i];
}

int nfc_ev2_compute_cmd_mac(const nfc_ev2_mac_ctx_t *ctx,
                            const nfc_ev2_cmd_data_t *cmd,
                            uint8_t mac8[8]) {
  if (ctx == NULL || ctx->ses_mac_key == NULL || ctx->ti == NULL || cmd == NULL || mac8 == NULL)
    return -1;

  size_t buf_len = EV2_MAC_HEADER_SIZE + cmd->hdr_len + cmd->data_len;
  uint8_t *buf = (uint8_t *)malloc(buf_len);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Cannot allocate MAC input");
    return -1;
  }

  buf[0] = ctx->ins;
  buf[1] = (uint8_t)(ctx->cmd_ctr & 0xFF);
  buf[2] = (uint8_t)((ctx->cmd_ctr >> 8) & 0xFF);
  memcpy(buf + 3, ctx->ti, EV2_TI_SIZE);
  if (cmd->hdr_len && cmd->header)
    memcpy(buf + EV2_MAC_HEADER_SIZE, cmd->header, cmd->hdr_len);
  if (cmd->data_len && cmd->data)
    memcpy(buf + EV2_MAC_HEADER_SIZE + cmd->hdr_len, cmd->data, cmd->data_len);

  uint8_t cmac[AES_BLOCK_SIZE];
  int result = nfc_aes_cmac(ctx->ses_mac_key, AES_BLOCK_SIZE, buf, buf_len, cmac);
  if (result == 0)
    nfc_ev2_mac_truncate(cmac, mac8);
  else
    memset(mac8, 0, DES_BLOCK_SIZE);
  free(buf);
  return result;
}

int nfc_ev2_compute_resp_mac(const nfc_ev2_mac_ctx_t *ctx,
                             const uint8_t *resp_data,
                             size_t resp_len,
                             uint8_t mac8[8]) {
  if (ctx == NULL || ctx->ses_mac_key == NULL || ctx->ti == NULL || mac8 == NULL)
    return -1;

  size_t buf_len = EV2_MAC_HEADER_SIZE + resp_len;
  uint8_t *buf = (uint8_t *)malloc(buf_len);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Cannot allocate MAC input");
    return -1;
  }

  buf[0] = ctx->ins;
  buf[1] = (uint8_t)(ctx->cmd_ctr & 0xFF);
  buf[2] = (uint8_t)((ctx->cmd_ctr >> 8) & 0xFF);
  memcpy(buf + 3, ctx->ti, EV2_TI_SIZE);
  if (resp_len && resp_data)
    memcpy(buf + EV2_MAC_HEADER_SIZE, resp_data, resp_len);

  uint8_t cmac[AES_BLOCK_SIZE];
  int result = nfc_aes_cmac(ctx->ses_mac_key, AES_BLOCK_SIZE, buf, buf_len, cmac);
  if (result == 0)
    nfc_ev2_mac_truncate(cmac, mac8);
  else
    memset(mac8, 0, DES_BLOCK_SIZE);
  free(buf);
  return result;
}

void nfc_ev2_build_iv_cmd(const uint8_t ses_enc[16],
                          const uint8_t ti[4],
                          uint16_t cmd_ctr,
                          uint8_t iv_out[16]) {
  uint8_t in[AES_BLOCK_SIZE] = {0};
  in[0] = EV2_SV_MARKER_A;
  in[1] = EV2_SV_MARKER_B;
  in[8] = (uint8_t)(cmd_ctr & 0xFF);
  in[9] = (uint8_t)((cmd_ctr >> 8) & 0xFF);
  memcpy(in + 10, ti, EV2_TI_SIZE);
  (void)nfc_aes_ecb_encrypt(ses_enc, in, iv_out);
}

void nfc_ev2_build_iv_resp(const uint8_t ses_enc[16],
                           const uint8_t ti[4],
                           uint16_t cmd_ctr_next,
                           uint8_t iv_out[16]) {
  uint8_t in[AES_BLOCK_SIZE] = {0};
  in[0] = EV2_SV_MARKER_B;
  in[1] = EV2_SV_MARKER_A;
  in[8] = (uint8_t)(cmd_ctr_next & 0xFF);
  in[9] = (uint8_t)((cmd_ctr_next >> 8) & 0xFF);
  memcpy(in + 10, ti, EV2_TI_SIZE);
  (void)nfc_aes_ecb_encrypt(ses_enc, in, iv_out);
}

size_t nfc_iso9797_pad_method2(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) {
  if (out == NULL || (in == NULL && in_len))
    return 0;
  size_t pad_len = AES_BLOCK_SIZE - (in_len % AES_BLOCK_SIZE);
  if (pad_len == 0)
    pad_len = AES_BLOCK_SIZE;
  size_t total = in_len + pad_len;
  if (total > out_max)
    return 0;
  if (in_len > 0)
    memcpy(out, in, in_len);
  out[in_len] = ISO9797_PAD_BYTE;
  if (pad_len > 1)
    memset(out + in_len + 1, 0x00, pad_len - 1);
  return total;
}

size_t nfc_iso9797_unpad_method2(uint8_t *buf, size_t len) {
  if (buf == NULL || len == 0 || (len % AES_BLOCK_SIZE) != 0)
    return 0;
  size_t i = len;
  while (i > 0 && buf[i - 1] == 0x00U)
    i--;
  if (i == 0 || buf[i - 1] != ISO9797_PAD_BYTE)
    return 0;
  return i - 1;
}

size_t nfc_iso9797_pad_method1(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) {
  if (out == NULL || (in == NULL && in_len))
    return 0;
  size_t pad_len = (AES_BLOCK_SIZE - (in_len % AES_BLOCK_SIZE)) % AES_BLOCK_SIZE;
  size_t total = in_len + pad_len;
  if (total > out_max)
    return 0;
  if (in_len > 0)
    memcpy(out, in, in_len);
  if (pad_len > 0)
    memset(out + in_len, 0x00, pad_len);
  return total;
}

int nfc_diversify_aes128(const uint8_t *master_key,
                         const uint8_t *div_input,
                         size_t div_input_len,
                         uint8_t *div_key) {
  if (master_key == NULL || div_input == NULL || div_key == NULL)
    return -1;
  if (div_input_len == 0 || div_input_len > DIVERSIFY_AES_MAX_INPUT)
    return -1;

  uint8_t m_padded[32];
  memset(m_padded, 0x00, sizeof(m_padded));
  memcpy(m_padded, div_input, div_input_len);
  m_padded[div_input_len] = ISO9797_PAD_BYTE;

  uint8_t sv[33];
  sv[0] = DIVERSIFY_SV_CONST_1;
  memcpy(sv + 1, m_padded, sizeof(m_padded));

  return nfc_aes_cmac(master_key, AES_BLOCK_SIZE, sv, sizeof(sv), div_key);
}

int nfc_diversify_desfire_key(const nfc_diversify_params_t *p, uint8_t *div_key) {
  if (p == NULL || p->master_key == NULL || p->uid_7b == NULL || div_key == NULL)
    return -1;

  uint8_t m[31];
  size_t m_len = 0;
  memcpy(m + m_len, p->uid_7b, DIVERSIFY_UID_LEN);
  m_len += DIVERSIFY_UID_LEN;
  if (p->aid_3b) {
    memcpy(m + m_len, p->aid_3b, DIVERSIFY_AID_LEN);
    m_len += DIVERSIFY_AID_LEN;
  }
  m[m_len++] = p->key_no;
  if (p->system_id && p->system_id_len > 0) {
    size_t copy = (m_len + p->system_id_len > sizeof(m)) ? (sizeof(m) - m_len) : p->system_id_len;
    memcpy(m + m_len, p->system_id, copy);
    m_len += copy;
  }
  return nfc_diversify_aes128(p->master_key, m, m_len, div_key);
}

int nfc_diversify_mfp_key(const uint8_t *master_key,
                          const uint8_t *uid_7b,
                          uint16_t block_addr,
                          uint8_t *div_key) {
  if (master_key == NULL || uid_7b == NULL || div_key == NULL)
    return -1;
  uint8_t m[9];
  memcpy(m, uid_7b, DIVERSIFY_UID_LEN);
  m[7] = (uint8_t)(block_addr & 0xFF);
  m[8] = (uint8_t)((block_addr >> 8) & 0xFF);
  return nfc_diversify_aes128(master_key, m, sizeof(m), div_key);
}

int nfc_diversify_2tdea(const nfc_diversify_params_t *p, uint8_t *div_key) {
  if (p == NULL || p->master_key == NULL || p->div_input == NULL || div_key == NULL)
    return -1;
  if (p->div_input_len == 0 || p->div_input_len > DIVERSIFY_2TDEA_MAX_INPUT)
    return -1;

  uint8_t m_padded[16];
  memset(m_padded, 0x00, sizeof(m_padded));
  memcpy(m_padded, p->div_input, p->div_input_len);
  m_padded[p->div_input_len] = ISO9797_PAD_BYTE;

  uint8_t sv[17];
  sv[0] = DIVERSIFY_SV_CONST_1;
  memcpy(sv + 1, m_padded, sizeof(m_padded));

  uint8_t cmac1[8];
  uint8_t cmac2[8];
  if (nfc_tdes_cmac(p->master_key, TDES_2KEY_LEN, sv, sizeof(sv), cmac1) != 0)
    return -1;

  sv[0] = DIVERSIFY_SV_CONST_2;
  if (nfc_tdes_cmac(p->master_key, TDES_2KEY_LEN, sv, sizeof(sv), cmac2) != 0)
    return -1;

  memcpy(div_key, cmac1, DES_BLOCK_SIZE);
  memcpy(div_key + DES_BLOCK_SIZE, cmac2, DES_BLOCK_SIZE);

  if (p->original_key) {
    div_key[DES_BLOCK_SIZE - 1] =
        (uint8_t)((div_key[DES_BLOCK_SIZE - 1] & DIVERSIFY_PARITY_MASK) |
                  (p->original_key[DES_BLOCK_SIZE - 1] & DIVERSIFY_PARITY_BIT));
    div_key[TDES_2KEY_LEN - 1] =
        (uint8_t)((div_key[TDES_2KEY_LEN - 1] & DIVERSIFY_PARITY_MASK) |
                  (p->original_key[TDES_2KEY_LEN - 1] & DIVERSIFY_PARITY_BIT));
  }
  return 0;
}
