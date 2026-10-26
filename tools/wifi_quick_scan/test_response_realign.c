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
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "spi_protocol.h"
#include "muse_response_realign.inc"

static void make_reply(uint8_t *frame, uint16_t cmd) {
  memset(frame, 0, SPI_FRAME_SIZE);
  spi_header_t *header = (spi_header_t *)frame;
  header->sync = SPI_SYNC_BYTE;
  header->type = SPI_TYPE_RESP;
  header->length = SPI_RESP_STATUS_SIZE;
  spi_header_set_cmd(header, cmd);
  frame[sizeof(*header)] = SPI_STATUS_OK;
  spi_frame_seal(header, header->length);
}

int main(void) {
  uint8_t frame[SPI_FRAME_SIZE], reply[SPI_FRAME_SIZE], original[SPI_FRAME_SIZE];
  const size_t reply_size = sizeof(spi_header_t) + SPI_RESP_STATUS_SIZE;
  const uint16_t scan_ids[] = {SPI_ID_WIFI_APP_SCAN_AP, SPI_ID_WIFI_SCAN_STATUS, SPI_ID_SYSTEM_DATA};
  for (size_t i = 0; i < sizeof(scan_ids) / sizeof(scan_ids[0]); i++) {
    make_reply(reply, scan_ids[i]);
    memset(frame, 0, sizeof(frame));
    memcpy(frame + 11, reply, reply_size);
    assert(muse_response_realign(frame, sizeof(frame), scan_ids[i]));
  }
  // The field trace contained the complete START response at bit 88 (byte 11).
  for (uint16_t cmd = SPI_ID_WIFI_QUICK_START; cmd <= SPI_ID_WIFI_QUICK_WHITELIST; cmd++) {
    make_reply(reply, cmd);
    memset(frame, 0, sizeof(frame));
    memcpy(frame + 11, reply, reply_size);
    assert(muse_response_realign(frame, sizeof(frame), cmd));
    assert(memcmp(frame, reply, reply_size) == 0);
  }
  make_reply(reply, SPI_ID_WIFI_QUICK_START);
  memset(frame, 0, sizeof(frame));
  memcpy(frame + 11, reply, reply_size);
  frame[11 + sizeof(spi_header_t)] ^= 1;
  memcpy(original, frame, sizeof(frame));
  assert(!muse_response_realign(frame, sizeof(frame), SPI_ID_WIFI_QUICK_START));
  assert(memcmp(frame, original, sizeof(frame)) == 0);
  memcpy(frame + 11, reply, reply_size);
  assert(!muse_response_realign(frame, sizeof(frame), SPI_ID_WIFI_QUICK_STATUS));
  memcpy(frame + 40, reply, reply_size);
  assert(!muse_response_realign(frame, sizeof(frame), SPI_ID_WIFI_QUICK_START));
  memset(frame, 0, sizeof(frame));
  memcpy(frame + sizeof(frame) - reply_size + 1, reply, reply_size - 1);
  assert(!muse_response_realign(frame, sizeof(frame), SPI_ID_WIFI_QUICK_START));
  make_reply(reply, SPI_ID_WIFI_START);
  memset(frame, 0, sizeof(frame));
  memcpy(frame + 11, reply, reply_size);
  assert(!muse_response_realign(frame, sizeof(frame), SPI_ID_WIFI_START));
  make_reply(reply, SPI_CMD(0x11, 1));
  memcpy(frame + 11, reply, reply_size);
  assert(muse_response_realign(frame, sizeof(frame), SPI_CMD(0x11, 1)));
  return 0;
}
