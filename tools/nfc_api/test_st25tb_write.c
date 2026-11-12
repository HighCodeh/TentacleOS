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
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM -1
#define HB_NFC_ERR_TIMEOUT -2
#define HB_NFC_ERR_PROTOCOL -3
#define ESP_ERR_INVALID_STATE -4
#define add_line(result, ...) ((void)(result))
#undef strlcpy
#define strlcpy(...) ((void)0)
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define pdMS_TO_TICKS(ms) (ms)
typedef int hb_nfc_err_t;
typedef struct { char heading[32]; } nfc_protocol_result_t;
static int s_mux;
static struct {bool running;} s_status;
static uint8_t s_st25tb_uid[8];
static uint16_t s_st25tb_blocks;
static bool s_has_st25tb;
static bool s_st25tb_write_pending;
static uint8_t s_st25tb_write_block, s_st25tb_write_data[4];
static unsigned step, fail_step, writes;
static bool wrong_uid, bad_verify, cancelled, unexpected_ack, bad_crc;
static uint8_t memory[4];
static unsigned delay_ms;
uint16_t iso15693_crc16(const uint8_t *, size_t);
static bool cancel_requested(void) { return cancelled; }
static int iso14443b_poller_init(void) { return 0; }
static void vTaskDelay(unsigned ticks) {delay_ms += ticks;}
static int nfc_poller_transceive(const uint8_t *tx, size_t len, bool crc,
                                 uint8_t *rx, size_t max, size_t min, int timeout) {
  (void)min; (void)timeout; assert(crc);
  step++;
  if (step == fail_step) return 0;
  size_t size;
  memset(rx, 0, max);
  if (step == 1) {
    assert(len == 2 && tx[0] == 6 && tx[1] == 0); size = 1; rx[0] = 0xA5;
  } else if (step == 2) {
    assert(len == 2 && tx[0] == 0x0E && tx[1] == 0xA5);
    size = 1; rx[0] = 0xA5;
  } else if (step == 3) {
    assert(len == 1 && tx[0] == 0x0B); size = 8;
    memcpy(rx, s_st25tb_uid, size);
    if (wrong_uid) rx[0] ^= 1;
  } else if (step == 4) {
    assert(len == 6 && tx[0] == 9 && tx[1] == 3);
    assert(!memcmp(tx + 2, "\x01\x23\x45\x67", 4));
    writes++;
    memcpy(memory, tx + 2, 4);
    return unexpected_ack ? 1 : 0;
  } else {
    assert(step == 5 && len == 2 && tx[0] == 8 && tx[1] == 3);
    assert(delay_ms >= 7);
    size = 4;
    memcpy(rx, memory, 4);
    if (bad_verify) rx[0] ^= 1;
  }
  uint16_t check = iso15693_crc16(rx, size);
  rx[size] = (uint8_t)check;
  rx[size + 1] = (uint8_t)(check >> 8);
  if (bad_crc) rx[size] ^= 1;
  return (int)size + 2;
}
/* PRODUCTION */
static void reset(void) {
  s_has_st25tb = true; s_st25tb_blocks = 16; s_status.running = false;
  step = fail_step = writes = delay_ms = 0;
  wrong_uid = bad_verify = cancelled = unexpected_ack = bad_crc = false;
  memset(memory, 0, sizeof(memory));
  assert(nfc_protocol_service_st25tb_write("03:01234567") == HB_NFC_OK);
  assert(s_st25tb_write_pending && !writes);
}
int main(void) {
  nfc_protocol_result_t result = {0};
  reset();
  assert(write_st25tb(&result) == HB_NFC_OK && writes == 1 && step == 5);
  assert(write_st25tb(&result) == ESP_ERR_INVALID_STATE && writes == 1);
  for (unsigned fail = 1; fail <= 5; fail++) {
    reset(); fail_step = fail;
    assert(write_st25tb(&result) != HB_NFC_OK);
    assert(!s_st25tb_write_pending);
    if (fail < 5) assert(!writes);
  }
  reset(); wrong_uid = true;
  assert(write_st25tb(&result) != HB_NFC_OK && !writes);
  reset(); cancelled = true;
  assert(write_st25tb(&result) == ESP_ERR_INVALID_STATE && !writes);
  reset(); bad_crc = true;
  assert(write_st25tb(&result) != HB_NFC_OK && !writes);
  reset(); bad_verify = true;
  assert(write_st25tb(&result) != HB_NFC_OK && writes == 1);
  reset(); unexpected_ack = true;
  assert(write_st25tb(&result) != HB_NFC_OK && writes == 1);
  reset();
  const char *bad[] = {"", "03:1234567", "03:012345678", "03-01234567", "FF:01234567",
                       "10:01234567", "03:0123456g"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    assert(nfc_protocol_service_st25tb_write(bad[i]) == HB_NFC_ERR_PARAM);
  assert(nfc_protocol_service_st25tb_write(NULL) == HB_NFC_OK && !s_st25tb_write_pending);
  assert(write_st25tb(&result) == ESP_ERR_INVALID_STATE);
  s_has_st25tb = false;
  assert(nfc_protocol_service_st25tb_write("03:01234567") == HB_NFC_ERR_PARAM);
  s_has_st25tb = true; s_status.running = true;
  assert(nfc_protocol_service_st25tb_write("03:01234567") == ESP_ERR_INVALID_STATE);
  puts("ST25TB: staged/one-shot writes, UID guard, read-back and failure tests passed");
}
