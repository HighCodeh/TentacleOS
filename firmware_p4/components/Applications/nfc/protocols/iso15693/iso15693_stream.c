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

#include <stdbool.h>
#include <string.h>

#define STREAM_TX_SOF       0x21U
#define STREAM_TX_EOF       0x04U
#define STREAM_TX_EXPANSION 4U
#define STREAM_RX_SOF       0x17U
#define STREAM_RX_EOF       0x1DU
#define STREAM_MARKER_BITS  5U
#define STREAM_PAIR_BITS    2U
#define STREAM_BYTE_BITS    8U
#define STREAM_PAIR_ZERO    1U
#define STREAM_PAIR_ONE     2U

size_t iso15693_stream_encode(const uint8_t *data, size_t len, uint8_t *out, size_t capacity) {
  if (data == NULL || out == NULL || len == 0 || capacity < 2U ||
      len > (capacity - 2U) / STREAM_TX_EXPANSION)
    return 0;
  size_t pos = 0;
  out[pos++] = STREAM_TX_SOF;
  for (size_t i = 0; i < len; i++) {
    for (size_t shift = 0; shift < STREAM_BYTE_BITS; shift += STREAM_PAIR_BITS)
      out[pos++] = (uint8_t)(1U << (1U + STREAM_PAIR_BITS * ((data[i] >> shift) & 3U)));
  }
  out[pos++] = STREAM_TX_EOF;
  return pos;
}

static uint8_t stream_bits(const uint8_t *raw, size_t pos, size_t count) {
  uint8_t value = 0;
  for (size_t bit = 0; bit < count; bit++, pos++)
    value |= ((raw[pos / STREAM_BYTE_BITS] >> (pos % STREAM_BYTE_BITS)) & 1U) << bit;
  return value;
}

size_t iso15693_stream_decode(const uint8_t *raw, size_t raw_bits, uint8_t *out, size_t capacity) {
  if (raw == NULL || out == NULL || capacity == 0 || raw_bits < 2U * STREAM_MARKER_BITS ||
      stream_bits(raw, 0, STREAM_MARKER_BITS) != STREAM_RX_SOF)
    return 0;
  memset(out, 0, capacity);
  size_t decoded_bits = 0;
  for (size_t pos = STREAM_MARKER_BITS; pos + STREAM_MARKER_BITS <= raw_bits;
       pos += STREAM_PAIR_BITS) {
    if (stream_bits(raw, pos, STREAM_MARKER_BITS) == STREAM_RX_EOF)
      return decoded_bits % STREAM_BYTE_BITS == 0 ? decoded_bits / STREAM_BYTE_BITS : 0;
    if (decoded_bits / STREAM_BYTE_BITS >= capacity)
      return 0;
    const uint8_t pair = stream_bits(raw, pos, STREAM_PAIR_BITS);
    if (pair != STREAM_PAIR_ZERO && pair != STREAM_PAIR_ONE)
      return 0;
    if (pair == STREAM_PAIR_ONE)
      out[decoded_bits / STREAM_BYTE_BITS] |= 1U << (decoded_bits % STREAM_BYTE_BITS);
    decoded_bits++;
  }
  return 0;
}
