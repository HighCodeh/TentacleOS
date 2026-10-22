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

#include "hb_muse_token_frame.h"
#include <string.h>
static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = UINT32_MAX;
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (unsigned b = 0; b < 8; ++b)
      c = (c >> 1) ^ (UINT32_C(0xedb88320) & (0u - (c & 1u)));
  }
  return ~c;
}
bool hb_muse_token_frame_decode(
    uint8_t *p, size_t bytes, const uint8_t nonce[16], char **token, size_t *n) {
  if (token)
    *token = NULL;
  if (n)
    *n = 0;
  if (!p || !nonce || !token || !n || bytes < HB_MUSE_TOKEN_HEADER + 4u ||
      bytes > HB_MUSE_TOKEN_FRAME_MAX || memcmp(p, "HBMU1", 5) || memcmp(p + 5, nonce, 16))
    return false;
  size_t length = (size_t)p[21] | (size_t)p[22] << 8;
  if (length < 6 || length > HB_MUSE_TOKEN_MAX || bytes != HB_MUSE_TOKEN_HEADER + length + 4u ||
      memcmp(p + 23, "mgst_", 5))
    return false;
  for (size_t i = 0; i < length; ++i) {
    uint8_t c = p[23 + i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '-' || c == '.'))
      return false;
  }
  size_t end = HB_MUSE_TOKEN_HEADER + length;
  uint32_t expected = (uint32_t)p[end] | (uint32_t)p[end + 1] << 8 | (uint32_t)p[end + 2] << 16 |
                      (uint32_t)p[end + 3] << 24;
  if (crc32(p, end) != expected)
    return false;
  p[end] = 0; /* CRC has been checked; make borrowed token NUL-terminated. */
  *token = (char *)p + HB_MUSE_TOKEN_HEADER;
  *n = length;
  return true;
}
