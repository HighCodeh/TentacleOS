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
/**
 * @file mf_ultralight.c
 * @brief MIFARE Ultralight / Ultralight-C read, write, and auth operations.
 * Reference: NXP MF0ICU1, MF0ICU2 — MIFARE Ultralight and Ultralight-C product spec.
 */
#include "mf_ultralight.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "legacy_des/des.h"

#include "nfc_poller.h"
#include "nfc_common.h"
#include "hb_nfc_timer.h"

static const char *TAG = "mf_ultralight";

#define MFUL_CMD_READ        0x30
#define MFUL_CMD_WRITE       0xA2
#define MFUL_CMD_GET_VERSION 0x60
#define MFUL_CMD_PWD_AUTH    0x1B
#define MFUL_CMD_ULC_AUTH    0x1A

#define MFUL_TIMEOUT_MS       30
#define MFUL_WRITE_TIMEOUT_MS 20

#define MFUL_AUTH_DELAY_US 500

#define DES_BLOCK_SIZE 8

#define MFUL_ULC_KEY_LEN 16

#define MFUL_PAGE_SIZE 4

#define MFUL_READ_PAGES 4

#define MFUL_ULC_CHALLENGE_LEN 8

#define MF_ACK_NIBBLE_MASK 0x0F
#define MF_ACK_VALUE       0x0A

typedef struct {
  bool encrypt;         /**< true for encrypt, false for decrypt. */
  const uint8_t *key;   /**< 16-byte 2-key 3DES key. */
  const uint8_t *iv_in; /**< 8-byte input IV. */
  uint8_t *iv_out;      /**< 8-byte output IV (NULL to discard). */
} des3_cbc_params_t;

static void ulc_random(uint8_t *out, size_t len) {
  for (size_t i = 0; i < len; i += sizeof(uint32_t)) {
    uint32_t r = esp_random();
    size_t left = len - i;
    size_t n = left < sizeof(uint32_t) ? left : sizeof(uint32_t);
    memcpy(&out[i], &r, n);
  }
}

static void rotate_left_8(uint8_t *out, const uint8_t *in) {
  out[0] = in[1];
  out[1] = in[2];
  out[2] = in[3];
  out[3] = in[4];
  out[4] = in[5];
  out[5] = in[6];
  out[6] = in[7];
  out[7] = in[0];
}

static bool
des3_cbc_crypt(const des3_cbc_params_t *p, const uint8_t *in, size_t len, uint8_t *out) {
  if (p == NULL || (len % DES_BLOCK_SIZE) != 0 || p->key == NULL || p->iv_in == NULL ||
      in == NULL || out == NULL)
    return false;

  mbedtls_des3_context ctx;
  mbedtls_des3_init(&ctx);
  int rc =
      p->encrypt ? mbedtls_des3_set2key_enc(&ctx, p->key) : mbedtls_des3_set2key_dec(&ctx, p->key);
  if (rc != 0) {
    mbedtls_des3_free(&ctx);
    return false;
  }

  uint8_t iv[DES_BLOCK_SIZE];
  memcpy(iv, p->iv_in, DES_BLOCK_SIZE);
  rc = mbedtls_des3_crypt_cbc(
      &ctx, p->encrypt ? MBEDTLS_DES_ENCRYPT : MBEDTLS_DES_DECRYPT, len, iv, in, out);
  if (p->iv_out != NULL)
    memcpy(p->iv_out, iv, DES_BLOCK_SIZE);
  mbedtls_des3_free(&ctx);
  return rc == 0;
}

int mful_read_pages(uint8_t page, uint8_t out[18]) {
  uint8_t cmd[2] = {MFUL_CMD_READ, page};
  return nfc_poller_transceive(cmd, sizeof(cmd), true, out, 18, 1, MFUL_TIMEOUT_MS);
}

hb_nfc_err_t mful_write_page(uint8_t page, const uint8_t data[4]) {
  uint8_t cmd[6] = {MFUL_CMD_WRITE, page, data[0], data[1], data[2], data[3]};
  uint8_t rx[MFUL_PAGE_SIZE] = {0};
  int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 1, MFUL_WRITE_TIMEOUT_MS);

  if (len >= 1 && (rx[0] & MF_ACK_NIBBLE_MASK) == MF_ACK_VALUE)
    return HB_NFC_OK;
  return HB_NFC_ERR_NACK;
}

int mful_get_version(uint8_t out[8]) {
  uint8_t cmd[1] = {MFUL_CMD_GET_VERSION};
  return nfc_poller_transceive(
      cmd, sizeof(cmd), true, out, DES_BLOCK_SIZE, 1, MFUL_WRITE_TIMEOUT_MS);
}

int mful_pwd_auth(const uint8_t pwd[4], uint8_t pack[2]) {
  uint8_t cmd[5] = {MFUL_CMD_PWD_AUTH, pwd[0], pwd[1], pwd[2], pwd[3]};
  uint8_t rx[MFUL_PAGE_SIZE] = {0};
  int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 2, MFUL_WRITE_TIMEOUT_MS);
  if (len >= 2) {
    pack[0] = rx[0];
    pack[1] = rx[1];
    hb_nfc_timer_delay_us(MFUL_AUTH_DELAY_US);
  }
  return len;
}

hb_nfc_err_t mful_ulc_auth(const uint8_t key[16]) {
  if (key == NULL)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd = MFUL_CMD_ULC_AUTH;
  uint8_t rx[MFUL_ULC_KEY_LEN] = {0};
  int len = nfc_poller_transceive(&cmd, 1, true, rx, sizeof(rx), 1, MFUL_TIMEOUT_MS);
  if (len < MFUL_ULC_CHALLENGE_LEN)
    return HB_NFC_ERR_PROTOCOL;

  uint8_t enc_rndb[DES_BLOCK_SIZE];
  memcpy(enc_rndb, rx, DES_BLOCK_SIZE);

  uint8_t rndb[DES_BLOCK_SIZE];
  uint8_t iv0[DES_BLOCK_SIZE] = {0};
  uint8_t iv1[DES_BLOCK_SIZE] = {0};
  if (!des3_cbc_crypt(
          &(des3_cbc_params_t){.encrypt = false, .key = key, .iv_in = iv0, .iv_out = iv1},
          enc_rndb,
          DES_BLOCK_SIZE,
          rndb)) {
    return HB_NFC_ERR_INTERNAL;
  }

  uint8_t rnda[DES_BLOCK_SIZE];
  ulc_random(rnda, sizeof(rnda));

  uint8_t rndb_rot[DES_BLOCK_SIZE];
  rotate_left_8(rndb_rot, rndb);

  uint8_t plain[MFUL_ULC_KEY_LEN];
  memcpy(plain, rnda, DES_BLOCK_SIZE);
  memcpy(&plain[DES_BLOCK_SIZE], rndb_rot, DES_BLOCK_SIZE);

  uint8_t enc2[MFUL_ULC_KEY_LEN];
  if (!des3_cbc_crypt(
          &(des3_cbc_params_t){.encrypt = true, .key = key, .iv_in = iv1, .iv_out = NULL},
          plain,
          sizeof(plain),
          enc2)) {
    return HB_NFC_ERR_INTERNAL;
  }

  uint8_t rx2[MFUL_ULC_KEY_LEN] = {0};
  len = nfc_poller_transceive(enc2, sizeof(enc2), true, rx2, sizeof(rx2), 1, MFUL_TIMEOUT_MS);
  if (len < MFUL_ULC_CHALLENGE_LEN)
    return HB_NFC_ERR_PROTOCOL;

  uint8_t enc_rnda_rot[DES_BLOCK_SIZE];
  memcpy(enc_rnda_rot, rx2, DES_BLOCK_SIZE);

  uint8_t iv2[DES_BLOCK_SIZE];
  memcpy(iv2, &enc2[DES_BLOCK_SIZE], DES_BLOCK_SIZE);

  uint8_t rnda_rot_dec[DES_BLOCK_SIZE];
  if (!des3_cbc_crypt(
          &(des3_cbc_params_t){.encrypt = false, .key = key, .iv_in = iv2, .iv_out = NULL},
          enc_rnda_rot,
          DES_BLOCK_SIZE,
          rnda_rot_dec)) {
    return HB_NFC_ERR_INTERNAL;
  }

  uint8_t rnda_rot[DES_BLOCK_SIZE];
  rotate_left_8(rnda_rot, rnda);
  if (memcmp(rnda_rot_dec, rnda_rot, DES_BLOCK_SIZE) != 0)
    return HB_NFC_ERR_AUTH;

  hb_nfc_timer_delay_us(MFUL_AUTH_DELAY_US);
  return HB_NFC_OK;
}

int mful_read_all(uint8_t *data, int max_pages) {
  int pages_read = 0;
  for (int pg = 0; pg < max_pages; pg += MFUL_READ_PAGES) {
    uint8_t buf[18] = {0};
    int len = mful_read_pages((uint8_t)pg, buf);
    if (len < (MFUL_READ_PAGES * MFUL_PAGE_SIZE))
      break;

    int pages_in_chunk = (max_pages - pg >= MFUL_READ_PAGES) ? MFUL_READ_PAGES : (max_pages - pg);
    for (int i = 0; i < pages_in_chunk; i++) {
      data[(pg + i) * MFUL_PAGE_SIZE + 0] = buf[i * MFUL_PAGE_SIZE + 0];
      data[(pg + i) * MFUL_PAGE_SIZE + 1] = buf[i * MFUL_PAGE_SIZE + 1];
      data[(pg + i) * MFUL_PAGE_SIZE + 2] = buf[i * MFUL_PAGE_SIZE + 2];
      data[(pg + i) * MFUL_PAGE_SIZE + 3] = buf[i * MFUL_PAGE_SIZE + 3];
    }
    pages_read = pg + pages_in_chunk;
  }
  return pages_read;
}
