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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "crypto1.h"
#define ESP_OK 0
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM -1
#define HB_NFC_ERR_AUTH -2
#define HB_NFC_ERR_INTERNAL -3
#define HB_NFC_ERR_TX_TIMEOUT -4
#define HB_NFC_ERR_TIMEOUT -5
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define nfc_log_hex(...) ((void)0)
#define ST25R3916_REG_ISO14443A 5
#define ST25R3916_REG_NUM_TX_BYTES1 0x1D
#define ST25R3916_REG_NUM_TX_BYTES2 0x1E
#define ST25R3916_CMD_STOP_ALL 0xC2
#define ST25R3916_CMD_CLEAR_FIFO 0xDB
#define ST25R3916_CMD_TX_WO_CRC 0xC5
typedef int hb_nfc_err_t;
typedef int esp_err_t;
typedef int mf_key_type_t;
typedef struct { uint8_t data[6]; } mf_classic_key_t;
/* CONSTANTS */
static crypto1_state_t s_crypto;
static crypto1_state_t s_card_crypto;
static bool s_auth;
static uint32_t s_last_nt, s_nr_state = 1;
static unsigned operations, fail_at, tx_bits;
static bool bad_at, bad_parity;
static int operation(void) { return ++operations == fail_at ? -1 : 0; }
static int hb_nfc_spi_reg_read(int reg, uint8_t *out) { (void)reg; *out = 0; return operation(); }
static int hb_nfc_spi_reg_write(int reg, uint8_t value) {
  if (reg == ST25R3916_REG_NUM_TX_BYTES1) tx_bits = (unsigned)value << 8;
  if (reg == ST25R3916_REG_NUM_TX_BYTES2) tx_bits |= value;
  return operation();
}
static int hb_nfc_spi_direct_cmd(int cmd) { (void)cmd; return operation(); }
static int st25r3916_irq_clear(void) { return operation(); }
static int st25r3916_irq_wait_txe(void) { assert(tx_bits == 72); return operation(); }
static int st25r3916_fifo_load(const uint8_t *data, size_t len) {
  assert(len == 9);
  crypto1_init(&s_card_crypto, UINT64_C(0xFFFFFFFFFFFF));
  crypto1_word(&s_card_crypto, UINT32_C(0x12345678) ^ UINT32_C(0xF0532F9C), 0);
  uint32_t ar = 0;
  size_t pos = 0;
  for (unsigned i = 0; i < 8; i++) {
    uint8_t enc = 0;
    for (unsigned bit = 0; bit < 8; bit++, pos++)
      enc |= ((data[pos / 8] >> (pos % 8)) & 1U) << bit;
    uint8_t plain = enc ^ crypto1_byte(&s_card_crypto, i < 4 ? enc : 0, i < 4);
    uint8_t parity = (data[pos / 8] >> (pos % 8)) & 1U;
    pos++;
    assert(parity == (crypto1_odd_parity8(plain) ^ crypto1_filter_output(&s_card_crypto)));
    if (i >= 4) ar = (ar << 8) | plain;
  }
  assert(ar == crypto1_prng_successor(UINT32_C(0x12345678), 64));
  return operation();
}
static int st25r3916_fifo_wait(size_t size, int timeout, uint16_t *count) {
  (void)timeout; *count = (uint16_t)size; return 0;
}
static int st25r3916_fifo_read(uint8_t *out, size_t len) {
  assert(len == 5); memset(out, 0, len);
  crypto1_state_t card = s_card_crypto;
  uint32_t at = crypto1_prng_successor(0x12345678, 96) ^ (bad_at ? 1U : 0U);
  size_t pos = 0;
  for (unsigned i = 0; i < 4; i++) {
    uint8_t plain = (uint8_t)(at >> (24 - i * 8));
    uint8_t enc = plain ^ crypto1_byte(&card, 0, 0);
    for (unsigned bit = 0; bit < 8; bit++, pos++)
      out[pos / 8] |= ((enc >> bit) & 1U) << (pos % 8);
    uint8_t parity = crypto1_filter_output(&card) ^ crypto1_odd_parity8(plain);
    if (bad_parity && i == 0) parity ^= 1;
    out[pos / 8] |= parity << (pos % 8); pos++;
  }
  return operation();
}
static int nfc_poller_classic_nonce(const uint8_t *tx, uint8_t *rx, int timeout) {
  (void)timeout;
  assert(tx[0] == 0x60);
  memcpy(rx, (uint8_t[]){0x12,0x34,0x56,0x78}, 4); return 4;
}
/* PRODUCTION */
int main(void) {
  mf_classic_key_t key = {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}};
  uint8_t uid[] = {0xF0,0x53,0x2F,0x9C};
  assert(mf_classic_auth(0, 0x60, &key, uid) == 0 && s_auth);
  unsigned successful_operations = operations;
  operations = 0; bad_at = true;
  assert(mf_classic_auth(0, 0x60, &key, uid) == HB_NFC_ERR_AUTH && !s_auth);
  operations = 0; bad_at = false; bad_parity = true;
  assert(mf_classic_auth(0, 0x60, &key, uid) == HB_NFC_ERR_AUTH && !s_auth);
  bad_parity = false;
  for (unsigned i = 1; i <= successful_operations; i++) {
    operations = 0; fail_at = i;
    assert(mf_classic_auth(0, 0x60, &key, uid) != HB_NFC_OK && !s_auth);
  }
  puts("Classic At/parity and bus-failure tests passed");
}
