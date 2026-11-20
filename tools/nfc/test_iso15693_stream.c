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
#include "iso15693_stream.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* CONSTANTS */
static uint8_t response[512], last_tx[512];
static size_t response_size, last_tx_size;
static int nfc_poller_transceive(const uint8_t *tx, size_t len, bool crc,
                                uint8_t *rx, size_t cap, size_t min, int timeout) {
  assert(!crc && min == 1 && timeout == 30);
  assert(len <= sizeof(last_tx) && cap >= response_size);
  memcpy(last_tx, tx, len); last_tx_size = len;
  memcpy(rx, response, response_size);
  return (int)response_size;
}
/* PRODUCTION */

static void bits(uint8_t *out, size_t *pos, unsigned value, unsigned count) {
  for (unsigned b = 0; b < count; b++, (*pos)++)
    if ((value >> b) & 1U) out[*pos / 8] |= 1U << (*pos % 8);
}
static size_t rx_frame(const uint8_t *data, size_t len, uint8_t *out) {
  memset(out, 0, 512);
  size_t pos = 0;
  bits(out, &pos, 0x17, 5);
  for (size_t i = 0; i < len; i++)
    for (unsigned b = 0; b < 8; b++)
      bits(out, &pos, ((data[i] >> b) & 1U) ? 2 : 1, 2);
  bits(out, &pos, 0x1D, 5);
  return pos;
}
int main(void) {
  uint8_t decoded[256], data[256], raw[512], tx[512];
  const uint8_t symbols[] = {0x21, 0x02, 0x08, 0x20, 0x80, 0x04};
  uint8_t value = 0xE4;
  assert(iso15693_stream_encode(&value, 1, tx, sizeof(tx)) == sizeof(symbols));
  assert(memcmp(tx, symbols, sizeof(symbols)) == 0);
  assert(!iso15693_stream_encode(&value, 1, tx, 5));
  assert(!iso15693_stream_encode(NULL, 1, tx, sizeof(tx)));
  assert(!iso15693_stream_encode(&value, (size_t)-1, tx, sizeof(tx)));
  for (size_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i * 73 + 19);
  unsigned checks = 0;
  for (size_t len = 1; len <= 254; len++) {
    const size_t count = rx_frame(data, len, raw);
    assert(iso15693_stream_decode(raw, count, decoded, sizeof(decoded)) == len);
    assert(memcmp(decoded, data, len) == 0);
    assert(!iso15693_stream_decode(raw, count, decoded, len - 1));
    for (size_t cut = 0; cut < count; cut++) {
      assert(!iso15693_stream_decode(raw, cut, decoded, sizeof(decoded))); checks++;
    }
    raw[0] ^= 1;
    assert(!iso15693_stream_decode(raw, count, decoded, sizeof(decoded)));
  }
  const uint8_t inventory[] = {0x26, 0x01, 0x00};
  assert(iso15693_crc16((const uint8_t *)"123456789", 9) == 0x906E);
  // Inventory CRC is F6 0A on the wire (LSB first).
  assert(iso15693_crc16(inventory, sizeof(inventory)) == 0x0AF6);
  const uint8_t expected_tx[] = {0x21,0x20,0x08,0x20,0x02,0x08,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x20,0x08,0x80,0x80,0x20,0x20,0x02,0x02,0x04};
  uint8_t answer[12] = {0,0,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0xE0};
  const uint16_t crc = iso15693_crc16(answer, 10);
  answer[10] = (uint8_t)crc; answer[11] = (uint8_t)(crc >> 8);
  size_t count = rx_frame(answer, sizeof(answer), response);
  response_size = (count + 7) / 8;
  assert(iso15693_transceive(inventory, sizeof(inventory), true, decoded, sizeof(decoded), 1, 30) == 12);
  assert(last_tx_size == sizeof(expected_tx) && memcmp(last_tx, expected_tx, sizeof(expected_tx)) == 0);
  assert(memcmp(decoded, answer, 12) == 0);
  answer[5] ^= 0x80; count = rx_frame(answer, sizeof(answer), response);
  assert(!iso15693_transceive(inventory, sizeof(inventory), true, decoded, sizeof(decoded), 1, 30));
  // A malformed Manchester pair must never become data or a successful frame.
  response[0] |= 0x60;
  assert(!iso15693_stream_decode(response, count, decoded, sizeof(decoded)));
  printf("NFC-V framing/CRC: %u truncations plus roundtrip, overflow and malformed tests PASS (host only)\n", checks);
  return 0;
}
