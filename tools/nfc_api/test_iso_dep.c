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
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM -1
#define HB_NFC_ERR_PROTOCOL -2
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define NFC_ATS_MAX_LEN 64
#define nfc_log_hex(...) ((void)0)
typedef int hb_nfc_err_t;
typedef struct { uint8_t ats[64]; size_t ats_len; uint8_t fsc, fwi; } nfc_iso_dep_data_t;
static uint8_t replies[4][300];
static size_t lengths[4], reply_index, reply_count, calls;
static bool repeat;
void iso14443a_crc(const uint8_t *, size_t, uint8_t [2]);
static int nfc_poller_transceive(const uint8_t *tx, size_t tx_len, bool crc,
                                 uint8_t *rx, size_t max, size_t min, int timeout) {
  (void)crc; (void)min; (void)timeout;
  calls++;
  assert(calls < 300);
  if (reply_index >= reply_count) return 0;
  if (reply_index == 1 && replies[0][0] == 0x12) assert(tx_len == 1 && tx[0] == 0xA3);
  size_t length = lengths[reply_index];
  assert(length + 2 <= max);
  memcpy(rx, replies[reply_index], length);
  iso14443a_crc(rx, length, rx + length);
  if (!repeat) reply_index++;
  return (int)length + 2;
}
/* PRODUCTION */
static void reset(void) {
  memset(replies, 0, sizeof(replies));
  memset(lengths, 0, sizeof(lengths));
  s_pcd_block_num = 0;
  calls = reply_index = 0;
  reply_count = 1;
  repeat = false;
}
int main(void) {
  nfc_iso_dep_data_t dep = {.fsc = 32, .fwi = 4};
  uint8_t tx[1] = {0}, out[16];
  reset(); replies[0][0] = 2; replies[0][1] = 0x90; replies[0][2] = 0; lengths[0] = 3;
  assert(iso_dep_transceive(&dep, tx, 1, out, sizeof(out), 20) == 2);
  assert(out[0] == 0x90 && s_pcd_block_num == 1);
  reset(); replies[0][0] = 2; lengths[0] = 20;
  assert(iso_dep_transceive(&dep, tx, 1, out, sizeof(out), 20) == 0);
  reset(); replies[0][0] = 0x12; replies[0][1] = 0xAB; lengths[0] = 2;
  assert(iso_dep_transceive(&dep, tx, 1, out, sizeof(out), 20) == 0);
  reset(); reply_count = 2; replies[0][0] = 0x12; replies[0][1] = 0xAB; lengths[0] = 2;
  replies[1][0] = 3; replies[1][1] = 0xCD; lengths[1] = 2;
  assert(iso_dep_transceive(&dep, tx, 1, out, sizeof(out), 20) == 2);
  assert(out[0] == 0xAB && out[1] == 0xCD);
  reset(); replies[0][0] = 3; lengths[0] = 2;
  assert(iso_dep_transceive(&dep, tx, 1, out, sizeof(out), 20) == 0);
  for (unsigned value = 0; value < 256; value++) {
    if (value >= 1 && value <= 59) continue;
    reset(); replies[0][0] = 0xF2; replies[0][1] = (uint8_t)value; lengths[0] = 2;
    assert(iso_dep_transceive(&dep, tx, 1, out, sizeof(out), 20) == 0);
    assert(calls == 1);
  }
  reset(); replies[0][0] = 0xF2; replies[0][1] = 1; lengths[0] = 2; repeat = true;
  assert(iso_dep_transceive(&dep, tx, 1, out, sizeof(out), 20) == 0);
  assert(calls == 257);
  reset(); replies[0][0] = 2; replies[0][1] = 0x78; lengths[0] = 2;
  assert(iso_dep_rats(8, 0, &dep) == HB_NFC_ERR_PROTOCOL);
  reset(); replies[0][0] = 2; replies[0][1] = 8; lengths[0] = 2;
  assert(iso_dep_rats(8, 0, &dep) == HB_NFC_OK);
  assert(iso_dep_rats(9, 0, &dep) == HB_NFC_ERR_PARAM);
  puts("ISO-DEP reader malformed-frame tests passed");
}
