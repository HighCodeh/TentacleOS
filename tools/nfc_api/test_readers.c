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
#define HB_NFC_ERR_NO_CARD -2
#define HB_NFC_ERR_PROTOCOL -3
#define HB_NFC_ERR_TIMEOUT -4
#define HB_NFC_ERR_NACK -5
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define nfc_log_hex(...) ((void)0)
typedef int hb_nfc_err_t;
/* CONSTANTS */
typedef struct {
  uint8_t idm[8], pmm[8], rd[2];
  bool rd_valid;
} felica_tag_t;
static uint8_t reply[256], command[64];
static size_t command_len;
static int reply_len;
static int nfc_poller_transceive(const uint8_t *tx, size_t len, bool crc,
                                 uint8_t *rx, size_t max, size_t min, int timeout) {
  (void)crc; (void)min; (void)timeout;
  assert(len <= sizeof(command));
  memcpy(command, tx, len);
  command_len = len;
  if (reply_len > (int)max) return 0;
  memcpy(rx, reply, (size_t)reply_len);
  return reply_len;
}
/* PRODUCTION */
int main(void) {
  felica_tag_t tag = {0};
  reply_len = 20;
  reply[0] = 20;
  reply[1] = 1;
  reply[2] = 0x12;
  assert(felica_sensf_req_slots(0xFFFF, 0, &tag) == HB_NFC_OK);
  const uint8_t polling[] = { 0, 0xFF, 0xFF, 1, 0};
  assert(command_len == sizeof(polling));
  assert(memcmp(command, polling, sizeof(polling)) == 0);
  assert(tag.idm[0] == 0x12 && tag.rd_valid);
  assert(felica_sensf_req_slots(0xFFFF, 2, &tag) == HB_NFC_ERR_PARAM);
  for (int len = 0; len < 18; len++) {
    reply_len = len;
    reply[0] = (uint8_t)len;
    assert(felica_sensf_req_slots(0xFFFF, 0, &tag) != HB_NFC_OK);
  }
  memset(&tag, 0, sizeof(tag));
  uint8_t output[16];
  for (unsigned blocks = 0; blocks < 15; blocks++) {
    memset(reply, 0, sizeof(reply));
    reply_len = 13 + (int)blocks * 16;
    reply[0] = (uint8_t)reply_len;
    reply[1] = 7;
    reply[12] = (uint8_t)blocks;
    memset(output, 0xA5, sizeof(output));
    int err = felica_read_blocks(&tag, 0x000B, 0, 1, output);
    assert((err == HB_NFC_OK) == (blocks == 1));
    if (blocks != 1) for (size_t i = 0; i < sizeof(output); i++) assert(output[i] == 0xA5);
  }
  reply_len = 29; reply[0] = 29; reply[12] = 1; reply[2] = 1;
  assert(felica_read_blocks(&tag, 0x000B, 0, 1, output) == HB_NFC_ERR_PROTOCOL);
  assert(felica_read_blocks(&tag, 0x000B, 255, 2, output) == HB_NFC_ERR_PARAM);
  memset(reply, 0, sizeof(reply)); reply_len = 12; reply[0] = 12; reply[1] = 9;
  assert(felica_write_blocks(&tag, 9, 0, 1, output) == HB_NFC_OK);
  reply[1] = 7;
  assert(felica_write_blocks(&tag, 9, 0, 1, output) == HB_NFC_ERR_PROTOCOL);
  puts("FeliCa reader malformed-frame tests passed");
}
