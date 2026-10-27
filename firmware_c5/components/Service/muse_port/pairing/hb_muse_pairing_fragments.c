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

#include "hb_muse_pairing_fragments.h"
#include "hb_muse_pairing.h"
#include <stdlib.h>
#include <string.h>
void hb_muse_pairing_fragments_release(uint8_t *json, size_t bytes) {
  volatile uint8_t *p = json;
  if (p)
    while (bytes--)
      *p++ = 0;
  free(json);
}
void hb_muse_pairing_fragments_drop(hb_muse_pairing_fragments_t *s) {
  if (!s)
    return;
  hb_muse_pairing_fragments_release(s->buffer, s->bytes);
  s->buffer = NULL;
  s->bytes = 0;
  s->connection = 0;
  s->started_ms = 0;
  s->total = s->next = 0;
}
void hb_muse_pairing_fragments_expire(hb_muse_pairing_fragments_t *s, uint64_t now) {
  if (!s)
    return;
  if (s->clock_started && now < s->last_ms)
    s->clock_fault = true;
  if (s->clock_fault || (s->buffer && now - s->started_ms >= HB_MUSE_PAIRING_FRAGMENT_TTL_MS))
    hb_muse_pairing_fragments_drop(s);
  if (!s->clock_started || now > s->last_ms)
    s->last_ms = now;
  s->clock_started = true;
}
hb_muse_fragment_result_t hb_muse_pairing_fragments_feed(hb_muse_pairing_fragments_t *s,
                                                         uint64_t connection,
                                                         const uint8_t *fragment,
                                                         size_t bytes,
                                                         uint64_t now,
                                                         uint8_t **complete,
                                                         size_t *complete_bytes) {
  if (complete)
    *complete = NULL;
  if (complete_bytes)
    *complete_bytes = 0;
  if (!s || !complete || !complete_bytes)
    return HB_MUSE_FRAGMENT_BAD;
  hb_muse_pairing_fragments_expire(s, now);
  if (s->clock_fault || !connection || !fragment || !bytes || bytes > HB_MUSE_PAIRING_ATT_MAX)
    goto bad;
  if (fragment[0] != 0xfeu) {
    hb_muse_pairing_fragments_drop(s);
    uint8_t *copy = calloc(1, bytes + 1u);
    if (!copy)
      goto bad;
    memcpy(copy, fragment, bytes);
    *complete = copy;
    *complete_bytes = bytes;
    return HB_MUSE_FRAGMENT_READY;
  }
  if (bytes <= 3u || !fragment[2] || fragment[1] >= fragment[2])
    goto bad;
  uint8_t index = fragment[1], total = fragment[2];
  if (!index) {
    hb_muse_pairing_fragments_drop(s);
    s->buffer = calloc(1, HB_MUSE_PAIRING_JSON_MAX + 1u);
    if (!s->buffer)
      goto bad;
    s->connection = connection;
    s->started_ms = now;
    s->total = total;
  }
  if (!s->buffer || s->connection != connection || total != s->total || index != s->next ||
      bytes - 3u > HB_MUSE_PAIRING_JSON_MAX - s->bytes)
    goto bad;
  memcpy(s->buffer + s->bytes, fragment + 3u, bytes - 3u);
  s->bytes += bytes - 3u;
  ++s->next;
  if (s->next < total)
    return HB_MUSE_FRAGMENT_MORE;
  *complete = s->buffer;
  *complete_bytes = s->bytes;
  s->buffer = NULL;
  hb_muse_pairing_fragments_drop(s);
  return HB_MUSE_FRAGMENT_READY;
bad:
  hb_muse_pairing_fragments_drop(s);
  return HB_MUSE_FRAGMENT_BAD;
}
bool hb_muse_pairing_fragment_encode(const uint8_t *json,
                                     size_t bytes,
                                     uint16_t mtu,
                                     uint8_t index,
                                     uint8_t out[HB_MUSE_PAIRING_NOTIFY_MAX],
                                     size_t *out_bytes,
                                     uint8_t *total) {
  if (out_bytes)
    *out_bytes = 0;
  if (total)
    *total = 0;
  if (!json || !bytes || bytes > HB_MUSE_PAIRING_JSON_MAX || !out || !out_bytes || !total ||
      mtu <= 6u)
    return false;
  size_t notify = (size_t)mtu - 3u;
  if (notify > HB_MUSE_PAIRING_NOTIFY_MAX)
    notify = HB_MUSE_PAIRING_NOTIFY_MAX;
  size_t payload = notify - 3u, count = (bytes + payload - 1u) / payload;
  if (count > UINT8_MAX || index >= count)
    return false;
  size_t offset = (size_t)index * payload, n = bytes - offset;
  if (n > payload)
    n = payload;
  out[0] = 0xfe;
  out[1] = index;
  out[2] = (uint8_t)count;
  memcpy(out + 3, json + offset, n);
  *out_bytes = n + 3u;
  *total = (uint8_t)count;
  return true;
}
