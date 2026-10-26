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

#ifndef FIRMWARE_P4_COMPONENTS_SERVICE_GROK_CLIENT_INCLUDE_GROK_WIRE_H
#define FIRMWARE_P4_COMPONENTS_SERVICE_GROK_CLIENT_INCLUDE_GROK_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  GROK_HELLO = 1,
  GROK_SESSION_START = 2,
  GROK_PTT_BEGIN = 3,
  GROK_PCM = 4,
  GROK_PTT_END = 5,
  GROK_CANCEL = 6,
  GROK_SESSION_STOP = 7,
  GROK_HELLO_OK = 0x81,
  GROK_STATE = 0x82,
  GROK_AUDIO = 0x83,
  GROK_TRANSCRIPT = 0x84,
  GROK_ERROR = 0x85,
  GROK_TURN_DONE = 0x86,
  GROK_FRAME_MAX = 1024,
  GROK_SAMPLE_RATE = 16000
};
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t header[3];
  uint8_t payload[GROK_FRAME_MAX + 1];
  size_t used, length;
} grok_wire_parser_t;
typedef bool (*grok_wire_callback_t)(void *, uint8_t, uint8_t *, size_t);

static inline uint32_t grok_read_be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline void grok_write_be32(uint8_t *p, uint32_t value) {
  p[0] = value >> 24;
  p[1] = value >> 16;
  p[2] = value >> 8;
  p[3] = value;
}
// A TCP read or SPI response can split anywhere, including inside the header.
// The callback owns no pointers past its call; rejected frames fail the session.
static inline bool grok_wire_feed(
    grok_wire_parser_t *p, const uint8_t *src, size_t n, grok_wire_callback_t callback, void *ctx) {
  if (!p || (!src && n) || !callback)
    return false;
  while (n) {
    if (p->used < 3) {
      p->header[p->used++] = *src++;
      --n;
      if (p->used < 3)
        continue;
      p->length = (size_t)p->header[1] * 256 + p->header[2];
      if (p->length > GROK_FRAME_MAX) {
        p->used = 0;
        return false;
      }
    }
    size_t have = p->used - 3;
    size_t take = p->length - have;
    if (take > n)
      take = n;
    if (take)
      memcpy(p->payload + have, src, take);
    p->used += take;
    src += take;
    n -= take;
    if (p->used == p->length + 3) {
      p->payload[p->length] = 0;
      if (!callback(ctx, p->header[0], p->payload, p->length)) {
        p->used = 0;
        return false;
      }
      p->used = 0;
    }
  }
  return true;
}

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_P4_COMPONENTS_SERVICE_GROK_CLIENT_INCLUDE_GROK_WIRE_H
