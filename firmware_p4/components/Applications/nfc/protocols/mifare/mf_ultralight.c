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
#include "iso14443a.h"
#include "hb_nfc_timer.h"

static const char *TAG = "mf_ultralight";

#define MFUL_UID_PREFIX_BYTES  3U
#define MFUL_CMD_AES_SIGNATURE 0x3CU
#define MFUL_CMD_AES_COUNTER   0x39U
#define MFUL_COUNTER_BYTES     3U
#define MFUL_BYTE_BITS         8U
#define MFUL_AES_PAGES         60U
#define MFUL_AES_KEY_PAGE      48U
#define MFUL_AES_USER_FIRST    4U
#define MFUL_AES_USER_END      40U
#define MFUL_AES_CRC_SIZE      2U
#define MFUL_PAGE_BYTES        4U
#define MFUL_READ_BYTES        16U

#define MFUL_I2C_MAJOR             2U
#define MFUL_I2C_ORIGINAL_MINOR    1U
#define MFUL_I2C_PLUS_MINOR        2U
#define MFUL_I2C_FAMILY            0x07U
#define MFUL_I2C_SIZE_1K           0x13U
#define MFUL_I2C_SIZE_2K           0x15U
#define MFUL_I2C_PAGES_1K          231U
#define MFUL_I2C_PAGES_2K          485U
#define MFUL_I2C_PLUS_PAGES_1K     236U
#define MFUL_I2C_PLUS_PAGES_2K     492U
#define MFUL_I2C_SECTOR_PAGES      256U
#define MFUL_I2C_BASE_1K           227U
#define MFUL_I2C_BASE_2K           481U
#define MFUL_I2C_PLUS_BASE         234U
#define MFUL_I2C_REGISTER_PAGES    2U
#define MFUL_I2C_CONFIG_PAGE       232U
#define MFUL_I2C_SESSION_PAGE      248U
#define MFUL_I2C_PLUS_SESSION_PAGE 236U
#define MFUL_I2C_SESSION_SECTOR    3U
#define MFUL_CMD_SECTOR_SELECT     0xC2U
#define MFUL_SECTOR_SELECT_PARAM   0xFFU
#define MFUL_SECTOR_TIMEOUT_MS     5
#define MFUL_VENDOR_NXP            0x04U
#define MFUL_FAMILY_UL             0x03U
#define MFUL_FAMILY_NTAG           0x04U
#define MFUL_SUBTYPE_I2C           0x05U
#define MFUL_VERSION_PROTOCOL      0x03U
#define MFUL_SIZE_SMALL            0x0BU
#define MFUL_SIZE_MEDIUM           0x0EU
#define MFUL_SIZE_213              0x0FU
#define MFUL_SIZE_215              0x11U
#define MFUL_SIZE_216              0x13U
#define MFUL_PAGES_SMALL           20U
#define MFUL_PAGES_MEDIUM          41U
#define MFUL_PAGES_213             45U
#define MFUL_PAGES_215             135U
#define MFUL_PAGES_216             231U
#define MFUL_CMD_READ              0x30
#define MFUL_CMD_WRITE             0xA2
#define MFUL_CMD_GET_VERSION       0x60
#define MFUL_CMD_PWD_AUTH          0x1B
#define MFUL_CMD_ULC_AUTH          0x1A

#define MFUL_TIMEOUT_MS       30
#define MFUL_WRITE_TIMEOUT_MS 20

#define MFUL_AUTH_DELAY_US 500

#define DES_BLOCK_SIZE 8

#define MFUL_ULC_KEY_LEN 16

#define MFUL_PAGE_SIZE 4

#define MFUL_READ_PAGES       4
#define MFUL_CRC_BYTES        2U
#define MFUL_READ_FRAME_BYTES (MFUL_PAGE_SIZE * MFUL_READ_PAGES + MFUL_CRC_BYTES)
#define MFUL_VERSION_BYTES    8U

#define MFUL_ULC_CHALLENGE_LEN  8
#define MFUL_AUTH_MORE          0xAFU
#define MFUL_AUTH_RESPONSE_SIZE (1 + MFUL_ULC_CHALLENGE_LEN + 2)

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
  if (out == NULL)
    return 0;
  uint8_t cmd[2] = {MFUL_CMD_READ, page};
  uint8_t received[MFUL_READ_FRAME_BYTES];
  const int len = nfc_poller_transceive(
      cmd, sizeof(cmd), true, received, sizeof(received), sizeof(received), MFUL_TIMEOUT_MS);
  if (len != sizeof(received) || !iso14443a_check_crc(received, sizeof(received)))
    return 0;
  memcpy(out, received, sizeof(received));
  return len;
}

hb_nfc_err_t mful_write_page(uint8_t page, const uint8_t data[4]) {
  if (data == NULL)
    return HB_NFC_ERR_PARAM;
  uint8_t cmd[6] = {MFUL_CMD_WRITE, page, data[0], data[1], data[2], data[3]};
  uint8_t rx[MFUL_PAGE_SIZE] = {0};
  int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 1, MFUL_WRITE_TIMEOUT_MS);

  if (len >= 1 && (rx[0] & MF_ACK_NIBBLE_MASK) == MF_ACK_VALUE)
    return HB_NFC_OK;
  return HB_NFC_ERR_NACK;
}

uint16_t mful_i2c_pages(const uint8_t version[8]) {
  if (version == NULL || version[0] != 0 || version[1] != MFUL_VENDOR_NXP ||
      (version[2] != MFUL_FAMILY_NTAG && version[2] != MFUL_I2C_FAMILY) ||
      version[3] != MFUL_SUBTYPE_I2C || version[4] != MFUL_I2C_MAJOR ||
      version[5] < MFUL_I2C_ORIGINAL_MINOR || version[5] > MFUL_I2C_PLUS_MINOR ||
      version[7] != MFUL_VERSION_PROTOCOL)
    return 0;
  bool is_plus = version[5] == MFUL_I2C_PLUS_MINOR;
  if (version[6] == MFUL_I2C_SIZE_1K)
    return is_plus ? MFUL_I2C_PLUS_PAGES_1K : MFUL_I2C_PAGES_1K;
  if (version[6] == MFUL_I2C_SIZE_2K)
    return is_plus ? MFUL_I2C_PLUS_PAGES_2K : MFUL_I2C_PAGES_2K;
  return 0;
}

static bool i2c_address(uint16_t total,
                        uint16_t linear,
                        uint8_t *out_sector,
                        uint8_t *out_page,
                        uint16_t *out_remaining) {
  if (linear >= total)
    return false;
  bool is_plus = total == MFUL_I2C_PLUS_PAGES_1K || total == MFUL_I2C_PLUS_PAGES_2K;
  uint16_t base = is_plus                      ? MFUL_I2C_PLUS_BASE
                  : total == MFUL_I2C_PAGES_1K ? MFUL_I2C_BASE_1K
                                               : MFUL_I2C_BASE_2K;
  if (is_plus && linear >= MFUL_I2C_PLUS_PAGES_1K) {
    *out_sector = 1;
    *out_page = linear - MFUL_I2C_PLUS_PAGES_1K;
    *out_remaining = total - linear;
  } else if (linear >= base) {
    uint16_t offset = linear - base;
    bool is_session = is_plus || offset >= MFUL_I2C_REGISTER_PAGES;
    if (!is_plus && is_session)
      offset -= MFUL_I2C_REGISTER_PAGES;
    *out_sector = is_plus                      ? 0
                  : is_session                 ? MFUL_I2C_SESSION_SECTOR
                  : total == MFUL_I2C_PAGES_2K ? 1
                                               : 0;
    *out_page = (is_plus      ? MFUL_I2C_PLUS_SESSION_PAGE
                 : is_session ? MFUL_I2C_SESSION_PAGE
                              : MFUL_I2C_CONFIG_PAGE) +
                offset;
    *out_remaining = MFUL_I2C_REGISTER_PAGES - offset;
  } else {
    *out_sector = linear / MFUL_I2C_SECTOR_PAGES;
    *out_page = linear % MFUL_I2C_SECTOR_PAGES;
    uint16_t sector_left = MFUL_I2C_SECTOR_PAGES - *out_page;
    *out_remaining = base - linear < sector_left ? base - linear : sector_left;
  }
  return true;
}

static bool select_sector(uint8_t sector) {
  const uint8_t command[] = {MFUL_CMD_SECTOR_SELECT, MFUL_SECTOR_SELECT_PARAM};
  uint8_t response[MFUL_PAGE_SIZE] = {0};
  int length = nfc_poller_transceive(
      command, sizeof(command), true, response, sizeof(response), 1, MFUL_SECTOR_TIMEOUT_MS);
  if (length != 1 || (response[0] & MF_ACK_NIBBLE_MASK) != MF_ACK_VALUE)
    return false;
  const uint8_t selection[] = {sector, 0, 0, 0};
  return nfc_poller_expect_silence(selection, sizeof(selection), MFUL_SECTOR_TIMEOUT_MS);
}

int mful_read_i2c(const uint8_t version[8], uint8_t *out_data, size_t capacity) {
  uint16_t total = mful_i2c_pages(version);
  if (!total || out_data == NULL || capacity < (size_t)total * MFUL_PAGE_SIZE)
    return 0;
  uint16_t linear = 0;
  uint8_t current_sector = MFUL_SECTOR_SELECT_PARAM;
  while (linear < total) {
    uint8_t sector, page;
    uint16_t remaining;
    if (!i2c_address(total, linear, &sector, &page, &remaining))
      break;
    if (sector != current_sector) {
      if (!select_sector(sector))
        break;
      current_sector = sector;
    }
    uint8_t response[MFUL_READ_PAGES * MFUL_PAGE_SIZE + 2];
    if (mful_read_pages(page, response) != sizeof(response))
      break;
    uint16_t count = remaining < MFUL_READ_PAGES ? remaining : MFUL_READ_PAGES;
    memcpy(out_data + linear * MFUL_PAGE_SIZE, response, count * MFUL_PAGE_SIZE);
    linear += count;
  }
  if (current_sector != 0 && current_sector != MFUL_SECTOR_SELECT_PARAM && !select_sector(0))
    return 0;
  return linear;
}

uint16_t mful_version_pages(const uint8_t version[8]) {
  if (version == NULL || version[0] != 0 || version[1] != MFUL_VENDOR_NXP ||
      version[7] != MFUL_VERSION_PROTOCOL || version[3] == MFUL_SUBTYPE_I2C)
    return 0;
  if (version[2] != MFUL_FAMILY_UL && version[2] != MFUL_FAMILY_NTAG)
    return 0;
  switch (version[6]) {
    case MFUL_SIZE_SMALL:
      return MFUL_PAGES_SMALL;
    case MFUL_SIZE_MEDIUM:
      return MFUL_PAGES_MEDIUM;
    case MFUL_SIZE_213:
      return version[2] == MFUL_FAMILY_NTAG ? MFUL_PAGES_213 : 0;
    case MFUL_SIZE_215:
      return version[2] == MFUL_FAMILY_NTAG ? MFUL_PAGES_215 : 0;
    case MFUL_SIZE_216:
      return version[2] == MFUL_FAMILY_NTAG ? MFUL_PAGES_216 : 0;
    default:
      return 0;
  }
}

int mful_get_version(uint8_t out[8]) {
  if (out == NULL)
    return 0;
  uint8_t cmd[1] = {MFUL_CMD_GET_VERSION};
  uint8_t received[MFUL_VERSION_BYTES + MFUL_CRC_BYTES];
  const int len = nfc_poller_transceive(
      cmd, sizeof(cmd), true, received, sizeof(received), sizeof(received), MFUL_WRITE_TIMEOUT_MS);
  if (len != sizeof(received) || !iso14443a_check_crc(received, sizeof(received)))
    return 0;
  memcpy(out, received, MFUL_VERSION_BYTES);
  return MFUL_VERSION_BYTES;
}

int mful_pwd_auth(const uint8_t pwd[4], uint8_t pack[2]) {
  if (pwd == NULL || pack == NULL)
    return 0;
  uint8_t cmd[5] = {MFUL_CMD_PWD_AUTH, pwd[0], pwd[1], pwd[2], pwd[3]};
  uint8_t rx[MFUL_PAGE_SIZE] = {0};
  int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 2, MFUL_WRITE_TIMEOUT_MS);
  if (len != MFUL_PAGE_SIZE || !iso14443a_check_crc(rx, (size_t)len))
    return 0;
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

  uint8_t cmd[] = {MFUL_CMD_ULC_AUTH, 0};
  uint8_t rx[MFUL_ULC_KEY_LEN] = {0};
  int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 1, MFUL_TIMEOUT_MS);
  if (len != MFUL_AUTH_RESPONSE_SIZE || rx[0] != MFUL_AUTH_MORE ||
      !iso14443a_check_crc(rx, (size_t)len))
    return HB_NFC_ERR_PROTOCOL;

  uint8_t enc_rndb[DES_BLOCK_SIZE];
  memcpy(enc_rndb, &rx[1], DES_BLOCK_SIZE);

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
  uint8_t continuation[1 + MFUL_ULC_KEY_LEN] = {MFUL_AUTH_MORE};
  memcpy(&continuation[1], enc2, sizeof(enc2));
  len = nfc_poller_transceive(
      continuation, sizeof(continuation), true, rx2, sizeof(rx2), 1, MFUL_TIMEOUT_MS);
  if (len != MFUL_AUTH_RESPONSE_SIZE || rx2[0] != 0 || !iso14443a_check_crc(rx2, (size_t)len))
    return HB_NFC_ERR_PROTOCOL;

  uint8_t enc_rnda_rot[DES_BLOCK_SIZE];
  memcpy(enc_rnda_rot, &rx2[1], DES_BLOCK_SIZE);

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
  if (data == NULL || max_pages <= 0 || max_pages > UINT8_MAX + 1)
    return 0;
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

bool mful_is_aes(const uint8_t version[8]) {
  static const uint8_t prefix[] = {0x00, 0x04, 0x03};
  return version && !memcmp(version, prefix, sizeof(prefix)) &&
         (version[3] == 1 || version[3] == 2) && version[4] == 4 && version[6] == 0x0FU &&
         version[7] == MFUL_VERSION_PROTOCOL;
}

esp_err_t
mful_aes_auth(t2t_aes_session_t *session, const uint8_t *key, uint8_t key_number, bool secure) {
  if (!session)
    return ESP_ERR_INVALID_ARG;
  t2t_aes_reset(session);
  if (!key || key_number > 1)
    return ESP_ERR_INVALID_ARG;
  uint8_t command[] = {MFUL_CMD_ULC_AUTH, key_number};
  uint8_t response[T2T_AES_AUTH_REPLY_SIZE + 2];
  uint8_t frame[T2T_AES_AUTH_CONTINUE_SIZE], nonce[T2T_AES_BLOCK_SIZE];
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  int length = nfc_poller_transceive(command,
                                     sizeof(command),
                                     true,
                                     response,
                                     sizeof(response),
                                     T2T_AES_AUTH_REPLY_SIZE,
                                     MFUL_TIMEOUT_MS);
  if (length != sizeof(response) || !iso14443a_check_crc(response, length))
    goto cleanup;
  esp_fill_random(nonce, sizeof(nonce));
  error = t2t_aes_reader_challenge(
      session, key, nonce, response, T2T_AES_AUTH_REPLY_SIZE, secure, frame);
  if (error != ESP_OK)
    goto cleanup;
  error = ESP_ERR_INVALID_RESPONSE;
  length = nfc_poller_transceive(frame,
                                 sizeof(frame),
                                 true,
                                 response,
                                 sizeof(response),
                                 T2T_AES_AUTH_REPLY_SIZE,
                                 MFUL_TIMEOUT_MS);
  if (length != sizeof(response) || !iso14443a_check_crc(response, length))
    goto cleanup;
  error = t2t_aes_reader_complete(session, response, T2T_AES_AUTH_REPLY_SIZE);
  if (error == ESP_OK)
    session->key_number = key_number;
cleanup:
  memset(nonce, 0, sizeof(nonce));
  memset(frame, 0, sizeof(frame));
  if (error != ESP_OK)
    t2t_aes_reset(session);
  return error;
}

static bool aes_exchange(t2t_aes_session_t *session,
                         uint8_t command_code,
                         uint8_t parameter,
                         uint8_t *out_data,
                         size_t data_size) {
  uint8_t command[2 + T2T_AES_MAC_SIZE] = {command_code, parameter};
  uint8_t response[T2T_AES_SIGNATURE_SIZE + T2T_AES_MAC_SIZE + 2];
  if (!session || !session->is_authenticated || !out_data || data_size > T2T_AES_SIGNATURE_SIZE)
    return false;
  size_t command_length = 2;
  if (session->is_secure) {
    if (session->counter == UINT16_MAX ||
        t2t_aes_sign(session, command, command_length, command + command_length) != ESP_OK)
      goto fail;
    command_length += T2T_AES_MAC_SIZE;
  }
  int length = nfc_poller_transceive(
      command, command_length, true, response, sizeof(response), data_size, MFUL_TIMEOUT_MS);
  size_t expected = data_size + (session->is_secure ? T2T_AES_MAC_SIZE : 0);
  if (length < 0 || (size_t)length != expected + 2 || !iso14443a_check_crc(response, length))
    goto fail;
  size_t verified_size = expected;
  if (session->is_secure && t2t_aes_verify(session, response, expected, &verified_size) != ESP_OK)
    goto fail;
  if (verified_size != data_size)
    goto fail;
  memcpy(out_data, response, data_size);
  return true;
fail:
  t2t_aes_reset(session);
  return false;
}

static int aes_read_memory(t2t_aes_session_t *session, uint8_t *out_data, size_t capacity) {
  if (!session || !session->is_authenticated || !out_data ||
      capacity < MFUL_AES_PAGES * MFUL_PAGE_BYTES)
    return 0;
  uint16_t pages = 0;
  for (; pages < MFUL_AES_PAGES; pages += MFUL_READ_PAGES) {
    if (!aes_exchange(session,
                      MFUL_CMD_READ,
                      (uint8_t)pages,
                      out_data + pages * MFUL_PAGE_BYTES,
                      MFUL_READ_BYTES))
      break;
  }
  return pages;
}

int mful_aes_read_all(t2t_aes_session_t *session, uint8_t *out_data, size_t capacity) {
  int pages = aes_read_memory(session, out_data, capacity);
  t2t_aes_reset(session);
  return pages;
}

esp_err_t
mful_aes_read_card(t2t_aes_session_t *session, const uint8_t version[8], nfc_t2t_file_t *out_card) {
  esp_err_t error = ESP_ERR_INVALID_ARG;
  if (!out_card)
    goto cleanup;
  memset(out_card, 0, sizeof(*out_card));
  if (!mful_is_aes(version))
    goto cleanup;
  error = ESP_ERR_INVALID_RESPONSE;
  if (aes_read_memory(session, out_card->memory, sizeof(out_card->memory)) != MFUL_AES_PAGES)
    goto cleanup;
  out_card->length = MFUL_AES_PAGES * MFUL_PAGE_BYTES;
  out_card->config.type = T2T_TAG_ULTRALIGHT_AES;
  out_card->config.uid_len = sizeof(out_card->config.uid);
  memcpy(out_card->config.uid, out_card->memory, MFUL_UID_PREFIX_BYTES);
  memcpy(out_card->config.uid + MFUL_UID_PREFIX_BYTES,
         out_card->memory + MFUL_PAGE_BYTES,
         MFUL_PAGE_BYTES);
  memcpy(out_card->config.version, version, T2T_VERSION_SIZE);
  out_card->config.has_version = true;
  size_t key_offset =
      MFUL_AES_KEY_PAGE * MFUL_PAGE_BYTES + session->key_number * T2T_AES_BLOCK_SIZE;
  for (size_t i = 0; i < T2T_AES_BLOCK_SIZE; i++)
    out_card->memory[key_offset + i] = session->key[T2T_AES_BLOCK_SIZE - 1 - i];
  out_card->config.aes_key_known[session->key_number] = UINT16_MAX;
  if (aes_exchange(session,
                   MFUL_CMD_AES_SIGNATURE,
                   0,
                   out_card->config.aes_signature,
                   T2T_AES_SIGNATURE_SIZE)) {
    out_card->config.has_aes_signature = true;
    out_card->config.aes_signature_known = T2T_AES_SIGNATURE_KNOWN;
  }
  for (uint8_t index = 0; index < T2T_COUNTER_COUNT && session->is_authenticated; index++) {
    uint8_t counter[MFUL_COUNTER_BYTES];
    if (!aes_exchange(session, MFUL_CMD_AES_COUNTER, index, counter, sizeof(counter)))
      break;
    out_card->config.counters[index] = counter[0] | ((uint32_t)counter[1] << MFUL_BYTE_BITS) |
                                       ((uint32_t)counter[2] << (2 * MFUL_BYTE_BITS));
    out_card->config.counter_known |= 1U << index;
  }
  error = ESP_OK;
cleanup:
  t2t_aes_reset(session);
  if (error != ESP_OK && out_card)
    memset(out_card, 0, sizeof(*out_card));
  return error;
}

esp_err_t
mful_aes_write_page(t2t_aes_session_t *session, uint8_t page, const uint8_t data[MFUL_PAGE_BYTES]) {
  if (!session || !data || !session->is_authenticated || session->key_number != 0 ||
      !((page >= MFUL_AES_USER_FIRST && page < MFUL_AES_USER_END) ||
        (page >= MFUL_AES_KEY_PAGE && page < MFUL_AES_KEY_PAGE + MFUL_READ_PAGES)))
    return ESP_ERR_INVALID_ARG;
  uint8_t command[2 + MFUL_PAGE_BYTES + T2T_AES_MAC_SIZE] = {MFUL_CMD_WRITE, page};
  memcpy(command + 2, data, MFUL_PAGE_BYTES);
  size_t length = 2 + MFUL_PAGE_BYTES;
  bool secure = session->is_secure;
  if (secure) {
    if (session->counter == UINT16_MAX ||
        t2t_aes_sign(session, command, length, command + length) != ESP_OK)
      goto fail;
    length += T2T_AES_MAC_SIZE;
  }
  uint8_t response[T2T_AES_MAC_SIZE + MFUL_AES_CRC_SIZE];
  int received = nfc_poller_transceive(command,
                                       length,
                                       true,
                                       response,
                                       sizeof(response),
                                       secure ? T2T_AES_MAC_SIZE : 1,
                                       MFUL_WRITE_TIMEOUT_MS);
  if (secure) {
    size_t body = 0;
    if (received != sizeof(response) || !iso14443a_check_crc(response, received) ||
        t2t_aes_verify(session, response, T2T_AES_MAC_SIZE, &body) != ESP_OK || body != 0)
      goto fail;
  } else if (received != 1 || (response[0] & MF_ACK_NIBBLE_MASK) != MF_ACK_VALUE)
    goto fail;
  return ESP_OK;
fail:
  t2t_aes_reset(session);
  return ESP_ERR_INVALID_RESPONSE;
}
