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
#define HB_PROTO_ST25TB 1
#define NFC_STORE_NAME_MAX 32
#define HB_NFC_ERR_PARAM -1
#define HB_NFC_ERR_TIMEOUT -2
#define HB_NFC_ERR_PROTOCOL -3
#define ESP_ERR_INVALID_STATE -4
#define add_line(result, ...) ((void)(result))
#define format_uid(uid, len, reverse, out, max) (memset(out, 0, max))
#undef strlcpy
#define strlcpy(...) ((void)0)
typedef int hb_nfc_err_t;
typedef struct { char heading[32]; } nfc_protocol_result_t;
static unsigned step, reads, fail_step, expected_blocks;
static bool bad_crc, bad_chip;
static uint8_t type;
static uint8_t s_st25tb_uid[8];
static uint16_t s_st25tb_blocks;
static bool s_has_st25tb;
static bool s_st25tb_write_pending;
typedef struct { const char *name;int protocol;const uint8_t *uid;size_t uid_len;
 const uint8_t *payload;size_t payload_len; } nfc_store_card_t;
static void nfc_store_init(void) {}
static int nfc_store_save(const nfc_store_card_t *card) {
 assert(card->payload_len==8+expected_blocks*4 && card->payload[0]==1);
 assert(card->payload[1]==expected_blocks && card->payload[3]==4);
 return 0;
}
uint16_t iso15693_crc16(const uint8_t *, size_t);
static bool cancel_requested(void) { return false; }
static int iso14443b_poller_init(void) { return 0; }
static int nfc_poller_transceive(const uint8_t *tx, size_t len, bool crc,
                                 uint8_t *rx, size_t max, size_t min, int timeout) {
  (void)crc; (void)min; (void)timeout;
  step++;
  if (step == fail_step) return 0;
  size_t size;
  memset(rx, 0, max);
  if (step == 1) {
    assert(len == 2 && tx[0] == 6 && tx[1] == 0); size = 1; rx[0] = 0xA5;
  } else if (step == 2) {
    assert(len == 2 && tx[0] == 0x0E && tx[1] == 0xA5);
    size = 1; rx[0] = bad_chip ? 0 : 0xA5;
  } else if (step == 3) {
    assert(len == 1 && tx[0] == 0x0B); size = 8;
    rx[5] = (uint8_t)(type << 2); rx[6] = 2; rx[7] = 0xD0;
  } else {
    assert(len == 2 && tx[0] == 8 && tx[1] == (reads == expected_blocks ? 255 : reads));
    reads++; size = 4;
  }
  uint16_t check = iso15693_crc16(rx, size);
  rx[size] = (uint8_t)check;
  rx[size + 1] = (uint8_t)(check >> 8);
  if (bad_crc) rx[size] ^= 1;
  return (int)size + 2;
}
typedef struct {const char *name;const uint8_t *data;size_t length;} nfc_archive_field_t;
static int save_capture(const char *device,const uint8_t *uid,size_t uid_len,
 const nfc_archive_field_t *fields,size_t count,nfc_protocol_result_t *result) {
 (void)uid;(void)result; assert(!strcmp(device,"ST25TB") && uid_len==8 && count==3);assert(fields[1].length==4 && fields[2].length==expected_blocks*4); return 0;
}
/* PRODUCTION */
int main(void) {
  nfc_protocol_result_t result;
  const uint8_t types[] = {0, 3, 4, 6, 7, 12, 15};
  for (size_t i = 0; i < sizeof(types); i++) {
    type = types[i]; step = reads = 0;
    expected_blocks = type == 15 ? 64 : (type == 0 || type == 3 || type == 7 ? 128 : 16);
    assert(read_st25tb(&result) == HB_NFC_OK);
    assert(reads == expected_blocks + 1 && step == expected_blocks + 4);
  }
  type = 4;
  for (unsigned fail = 1; fail <= 20; fail++) {
    step = reads = 0; fail_step = fail;
    assert(read_st25tb(&result) != HB_NFC_OK);
    assert(step == fail);
  }
  fail_step = 0; step = reads = 0; bad_crc = true;
  assert(read_st25tb(&result) == HB_NFC_ERR_PROTOCOL);
  bad_crc = false; step = 0; bad_chip = true;
  assert(read_st25tb(&result) == HB_NFC_ERR_PROTOCOL);
  bad_chip = false; step = 0; type = 63;
  assert(read_st25tb(&result) == HB_NFC_ERR_PROTOCOL);
  puts("ST25TB reader sequence and fault tests passed");
}
