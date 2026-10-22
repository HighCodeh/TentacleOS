// SPDX-License-Identifier: GPL-3.0-or-later
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

/* Recover only a complete, unique, byte-aligned Muse reply within this
 * transfer. Never replay a command, combine polls, or repair payload/CRC. */
static bool muse_response_realign(uint8_t *buf, size_t size, uint16_t cmd) {
  if ((SPI_CMD_CAT(cmd) != 0x11 && cmd != 0x0001 && cmd != 0x000d && cmd != 0x1003) ||
      size != SPI_FRAME_SIZE) return false;
  size_t found = 0, bytes = 0;
  for (size_t off = 1; off + sizeof(spi_header_t) < size; ++off) {
    const spi_header_t *h = (const spi_header_t *)(buf + off);
    if (h->sync != SPI_SYNC_BYTE || h->type != SPI_TYPE_RESP ||
        spi_header_cmd(h) != cmd || h->length < SPI_RESP_STATUS_SIZE) continue;
    size_t total = sizeof(*h) + h->length;
    if (total > size - off || !spi_frame_valid(h, h->length)) continue;
    if (found) return false; /* Ambiguous: do not select a response. */
    found = off;
    bytes = total;
  }
  if (!found) return false;
  memmove(buf, buf + found, bytes);
  memset(buf + bytes, 0, size - bytes);
  return true;
}
