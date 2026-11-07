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
#define HB_NFC_ERR_TX_TIMEOUT -2
#define ST25R3916_CMD_TX_WO_CRC 1
#define ST25R3916_CMD_CLEAR_FIFO 2
#define MFC_TX_PARTIAL_BITS_MAX 7
#define ESP_LOGW(...) ((void)0)
typedef int esp_err_t;
typedef int hb_nfc_err_t;
typedef struct { int unused; } st25r3916_irq_status_t;
static struct { crypto1_state_t crypto; } s_emu;
static size_t s_loaded, s_bits;
static int s_wait_result;
static uint8_t s_fifo[24];
static void st25r3916_fifo_set_tx_bytes(uint16_t bytes, uint8_t bits) {
  s_bits = bytes * 8 + bits;
}
static void st25r3916_fifo_load(const uint8_t *data, size_t count) {
  assert(count <= sizeof(s_fifo));
  memcpy(s_fifo, data, count);
  s_loaded = count;
}
static int st25r3916_irq_read(st25r3916_irq_status_t *status) {
  (void)status;
  return ESP_OK;
}
static void hb_nfc_spi_direct_cmd(int cmd) { (void)cmd; }
static int st25r3916_irq_wait_txe(void) { return s_wait_result; }

static int hb_nfc_spi_target_tx_bits(const uint8_t *data, size_t bits) {
  s_bits = bits;
  st25r3916_fifo_load(data, (bits + 7) / 8);
  return s_wait_result;
}

/* PRODUCTION */

int main(void) {
  uint8_t plain[19] = {0x12, 0x34, 0x56, 0x78};
  assert(target_tx_raw(plain, 4, 0) == HB_NFC_OK);
  assert(s_bits == 32 && s_loaded == 4);
  assert(target_tx_raw(plain, 1, 4) == HB_NFC_OK);
  assert(s_bits == 4 && s_loaded == 1);
  assert(target_tx_raw(plain, 0, 4) == HB_NFC_ERR_PARAM);
  assert(target_tx_raw(plain, 1, 8) == HB_NFC_ERR_PARAM);

  for (size_t count = 1; count <= 18; count++) {
    crypto1_init(&s_emu.crypto, UINT64_C(0xffffffffffff));
    crypto1_state_t reader = s_emu.crypto;
    assert(target_tx_encrypted(plain, count) == HB_NFC_OK);
    assert(s_bits == count * 9);
    assert(s_loaded == (s_bits + 7) / 8);
    size_t bit = 0;
    for (size_t i = 0; i < count; i++) {
      uint8_t encrypted = 0;
      for (unsigned j = 0; j < 8; j++, bit++)
        encrypted |= ((s_fifo[bit / 8] >> (bit % 8)) & 1) << j;
      uint8_t decoded = encrypted ^ crypto1_byte(&reader, 0, 0);
      assert(decoded == plain[i]);
      uint8_t parity = (s_fifo[bit / 8] >> (bit % 8)) & 1;
      assert((parity ^ crypto1_filter_output(&reader)) == crypto1_odd_parity8(decoded));
      bit++;
    }
    assert(reader.odd == s_emu.crypto.odd && reader.even == s_emu.crypto.even);
  }
  assert(target_tx_encrypted(plain, 19) == HB_NFC_ERR_PARAM);
  assert(target_tx_ack_encrypted(0x0a) == HB_NFC_OK);
  assert(s_bits == 4 && s_loaded == 1);
  s_wait_result = -1;
  assert(target_tx_encrypted(plain, 4) == HB_NFC_ERR_TX_TIMEOUT);
  puts("NFC encrypted TX: exact bit lengths, parity and cipher state passed");
  return 0;
}
