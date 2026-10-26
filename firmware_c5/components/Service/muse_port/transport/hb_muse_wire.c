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

#include "hb_muse_wire.h"

#include <string.h>

static uint16_t read16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}
static uint32_t read32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void write32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}
static bool valid_frame(const hb_muse_wire_frame_t *f) {
  return f && f->opcode >= HB_MUSE_WIRE_HELLO && f->opcode <= HB_MUSE_WIRE_CLOSE &&
         f->payload_len <= HB_MUSE_WIRE_PAYLOAD_MAX && f->status <= HB_MUSE_WIRE_BUSY &&
         (f->flags == 0 || f->flags == HB_MUSE_WIRE_RESPONSE) &&
         (f->flags == HB_MUSE_WIRE_RESPONSE || f->status == HB_MUSE_WIRE_OK);
}
uint32_t hb_muse_wire_crc32(const uint8_t *bytes, size_t length) {
  uint32_t crc = UINT32_MAX;
  if (!bytes && length)
    return 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
  }
  return crc ^ UINT32_MAX;
}
bool hb_muse_wire_encode(const hb_muse_wire_frame_t *f,
                         uint8_t *bytes,
                         size_t capacity,
                         size_t *length) {
  if (length)
    *length = 0;
  if (!length || !bytes || !valid_frame(f))
    return false;
  const size_t total = HB_MUSE_WIRE_HEADER_BYTES + (size_t)f->payload_len + HB_MUSE_WIRE_CRC_BYTES;
  if (capacity < total)
    return false;
  write16(bytes, HB_MUSE_WIRE_MAGIC);
  bytes[2] = HB_MUSE_WIRE_VERSION;
  bytes[3] = f->opcode;
  write32(bytes + 4, f->boot);
  write32(bytes + 8, f->epoch);
  write32(bytes + 12, f->sequence);
  write32(bytes + 16, f->generation);
  write32(bytes + 20, f->turn);
  write16(bytes + 24, f->payload_len);
  bytes[26] = f->status;
  bytes[27] = f->flags;
  if (f->payload_len)
    memcpy(bytes + HB_MUSE_WIRE_HEADER_BYTES, f->payload, f->payload_len);
  write32(bytes + total - HB_MUSE_WIRE_CRC_BYTES,
          hb_muse_wire_crc32(bytes, total - HB_MUSE_WIRE_CRC_BYTES));
  *length = total;
  return true;
}
bool hb_muse_wire_decode(const uint8_t *bytes, size_t length, hb_muse_wire_frame_t *out) {
  if (!bytes || !out || length < HB_MUSE_WIRE_HEADER_BYTES + HB_MUSE_WIRE_CRC_BYTES ||
      length > HB_MUSE_WIRE_FRAME_MAX || read16(bytes) != HB_MUSE_WIRE_MAGIC ||
      bytes[2] != HB_MUSE_WIRE_VERSION)
    return false;
  const uint16_t payload_len = read16(bytes + 24);
  if (payload_len > HB_MUSE_WIRE_PAYLOAD_MAX ||
      length != HB_MUSE_WIRE_HEADER_BYTES + (size_t)payload_len + HB_MUSE_WIRE_CRC_BYTES ||
      read32(bytes + length - HB_MUSE_WIRE_CRC_BYTES) !=
          hb_muse_wire_crc32(bytes, length - HB_MUSE_WIRE_CRC_BYTES))
    return false;
  hb_muse_wire_frame_t frame = {0};
  frame.opcode = bytes[3];
  frame.boot = read32(bytes + 4);
  frame.epoch = read32(bytes + 8);
  frame.sequence = read32(bytes + 12);
  frame.generation = read32(bytes + 16);
  frame.turn = read32(bytes + 20);
  frame.payload_len = payload_len;
  frame.status = bytes[26];
  frame.flags = bytes[27];
  if (!valid_frame(&frame))
    return false;
  if (payload_len)
    memcpy(frame.payload, bytes + HB_MUSE_WIRE_HEADER_BYTES, payload_len);
  *out = frame;
  return true;
}

bool hb_muse_wire_server_init(hb_muse_wire_server_t *s,
                              uint32_t boot,
                              uint32_t lease,
                              const hb_muse_wire_callbacks_t *callbacks,
                              void *user) {
  if (!s)
    return false;
  memset(s, 0, sizeof(*s));
  if (!boot || !callbacks || !callbacks->can_open || !callbacks->dispatch || !callbacks->revoke)
    return false;
  s->callbacks = *callbacks;
  s->user = user;
  s->boot = boot;
  s->lease_ms = lease ? lease : HB_MUSE_WIRE_DEFAULT_LEASE_MS;
  s->initialized = true;
  return true;
}
static void revoke(hb_muse_wire_server_t *s) {
  if (!s->active)
    return;
  s->active = false;
  s->cache_valid = s->cache_is_close = false;
  memset(s->cached_request, 0, sizeof(s->cached_request));
  memset(s->cached_response, 0, sizeof(s->cached_response));
  s->cached_request_len = s->cached_response_len = 0;
  s->callbacks.revoke(s->user);
}
void hb_muse_wire_server_expire(hb_muse_wire_server_t *s, uint64_t now) {
  if (!s || !s->initialized)
    return;
  if (s->clock_fault)
    return;
  if (s->clock_started && now < s->last_clock_ms) {
    s->clock_fault = true;
    revoke(s);
    return;
  }
  s->clock_started = true;
  s->last_clock_ms = now;
  if (s->active && now - s->last_activity_ms >= s->lease_ms)
    revoke(s);
}
static void response_header(const hb_muse_wire_server_t *s,
                            const hb_muse_wire_frame_t *request,
                            hb_muse_wire_status_t status,
                            hb_muse_wire_frame_t *response) {
  memset(response, 0, sizeof(*response));
  response->opcode = request->opcode;
  response->boot = s->boot;
  response->epoch = request->epoch;
  response->sequence = request->sequence;
  response->generation = request->generation;
  response->turn = request->turn;
  response->flags = HB_MUSE_WIRE_RESPONSE;
  response->status = (uint8_t)status;
}
static bool has_nonce(const hb_muse_wire_frame_t *f) {
  if (f->payload_len != HB_MUSE_WIRE_NONCE_BYTES)
    return false;
  uint8_t any = 0;
  for (size_t i = 0; i < HB_MUSE_WIRE_NONCE_BYTES; ++i)
    any |= f->payload[i];
  return any != 0;
}
static bool respond(const hb_muse_wire_server_t *s,
                    const hb_muse_wire_frame_t *request,
                    hb_muse_wire_status_t status,
                    uint8_t *bytes,
                    size_t capacity,
                    size_t *length) {
  hb_muse_wire_frame_t response;
  response_header(s, request, status, &response);
  if (request->opcode == HB_MUSE_WIRE_OPEN && has_nonce(request)) {
    response.payload_len = HB_MUSE_WIRE_OPEN_BYTES;
    memcpy(response.payload, request->payload, HB_MUSE_WIRE_NONCE_BYTES);
  }
  return hb_muse_wire_encode(&response, bytes, capacity, length);
}
static void cache(hb_muse_wire_server_t *s,
                  const uint8_t *request,
                  size_t request_len,
                  const uint8_t *response,
                  size_t response_len,
                  bool closed) {
  /* Do not retain old PCM/text in the unused tail of a shorter replacement.
   * Ordinary memory hygiene, not a compiler/hardware secure-erasure claim. */
  memset(s->cached_request, 0, sizeof(s->cached_request));
  memset(s->cached_response, 0, sizeof(s->cached_response));
  memcpy(s->cached_request, request, request_len);
  s->cached_request_len = request_len;
  memcpy(s->cached_response, response, response_len);
  s->cached_response_len = response_len;
  s->cache_valid = true;
  s->cache_is_close = closed;
}
static bool ids_zero(const hb_muse_wire_frame_t *f) {
  return !f->generation && !f->turn;
}
bool hb_muse_wire_server_process(hb_muse_wire_server_t *s,
                                 const uint8_t *request,
                                 size_t request_len,
                                 uint64_t now,
                                 uint8_t *response,
                                 size_t capacity,
                                 size_t *response_len) {
  if (response_len)
    *response_len = 0;
  if (!s || !s->initialized || !response || !response_len || capacity < HB_MUSE_WIRE_FRAME_MAX)
    return false;
  hb_muse_wire_frame_t f;
  if (!hb_muse_wire_decode(request, request_len, &f) || f.flags != 0)
    return false;
  /* Permit caller reuse of the SPI input buffer for the response. Cache the
   * original bytes, never the response which may overwrite that buffer. */
  uint8_t original[HB_MUSE_WIRE_FRAME_MAX];
  memcpy(original, request, request_len);
  request = original;
  /* Invalid control shapes are rejected without invoking callbacks or time
   * mutation. A valid request may independently discover an expired lease. */
  if (f.opcode == HB_MUSE_WIRE_HELLO &&
      (f.boot || f.epoch || f.sequence || !ids_zero(&f) || !has_nonce(&f)))
    return respond(s, &f, HB_MUSE_WIRE_REJECTED, response, capacity, response_len);
  if (f.opcode == HB_MUSE_WIRE_HELLO) {
    hb_muse_wire_frame_t out;
    response_header(s, &f, HB_MUSE_WIRE_OK, &out);
    out.payload_len = HB_MUSE_WIRE_HELLO_BYTES;
    write32(out.payload, s->max_epoch);
    out.payload[4] = s->active ? 1u : 0u;
    memcpy(out.payload + 5, f.payload, HB_MUSE_WIRE_NONCE_BYTES);
    return hb_muse_wire_encode(&out, response, capacity, response_len);
  }
  hb_muse_wire_server_expire(s, now);
  if (s->clock_fault)
    return respond(s, &f, HB_MUSE_WIRE_REJECTED, response, capacity, response_len);
  if (f.boot != s->boot)
    return respond(s, &f, HB_MUSE_WIRE_STALE, response, capacity, response_len);
  if (s->cache_valid && f.epoch == s->active_epoch && f.sequence == s->last_sequence &&
      (s->active || s->cache_is_close)) {
    if (request_len != s->cached_request_len || memcmp(request, s->cached_request, request_len))
      return respond(s, &f, HB_MUSE_WIRE_CONFLICT, response, capacity, response_len);
    memcpy(response, s->cached_response, s->cached_response_len);
    *response_len = s->cached_response_len;
    return true;
  }
  if (f.opcode == HB_MUSE_WIRE_OPEN) {
    if (!f.epoch || f.epoch == UINT32_MAX || f.sequence != 1 || !ids_zero(&f) || !has_nonce(&f))
      return respond(s, &f, HB_MUSE_WIRE_REJECTED, response, capacity, response_len);
    if (f.epoch <= s->max_epoch)
      return respond(s, &f, HB_MUSE_WIRE_STALE, response, capacity, response_len);
    if (s->active || !s->callbacks.can_open(s->user))
      return respond(s, &f, HB_MUSE_WIRE_BUSY, response, capacity, response_len);
    if (!respond(s, &f, HB_MUSE_WIRE_OK, response, capacity, response_len))
      return false;
    s->active = true;
    s->active_epoch = s->max_epoch = f.epoch;
    s->last_sequence = 1;
    s->last_activity_ms = now;
    cache(s, request, request_len, response, *response_len, false);
    if (s->callbacks.opened)
      s->callbacks.opened(s->user, f.epoch);
    return true;
  }
  if (!s->active || f.epoch != s->active_epoch)
    return respond(s, &f, HB_MUSE_WIRE_STALE, response, capacity, response_len);
  if (!f.sequence || f.sequence < s->last_sequence)
    return respond(s, &f, HB_MUSE_WIRE_STALE, response, capacity, response_len);
  if (s->last_sequence == UINT32_MAX || f.sequence != s->last_sequence + 1u)
    return respond(s, &f, HB_MUSE_WIRE_GAP, response, capacity, response_len);
  hb_muse_wire_frame_t out;
  response_header(s, &f, HB_MUSE_WIRE_OK, &out);
  bool closed = false;
  if (f.opcode == HB_MUSE_WIRE_CLOSE) {
    if (!ids_zero(&f) || f.payload_len)
      out.status = HB_MUSE_WIRE_REJECTED;
    else if (!s->callbacks.can_open(s->user))
      out.status = HB_MUSE_WIRE_BUSY;
    else
      closed = true;
  } else {
    hb_muse_wire_status_t status =
        s->callbacks.dispatch(s->user, &f, out.payload, &out.payload_len);
    if ((status != HB_MUSE_WIRE_OK && status != HB_MUSE_WIRE_BLOCKED &&
         status != HB_MUSE_WIRE_REJECTED && status != HB_MUSE_WIRE_BUSY) ||
        out.payload_len > HB_MUSE_WIRE_PAYLOAD_MAX) {
      /* Callback contract violation: acceptance is uncertain. Never
       * replay dispatch; revoke the epoch and require a real fence. */
      revoke(s);
      return respond(s, &f, HB_MUSE_WIRE_REJECTED, response, capacity, response_len);
    }
    out.status = (uint8_t)status;
  }
  if (!hb_muse_wire_encode(&out, response, capacity, response_len)) {
    revoke(s);
    return false;
  }
  s->last_sequence = f.sequence;
  s->last_activity_ms = now;
  cache(s, request, request_len, response, *response_len, closed);
  if (closed)
    s->active = false;
  return true;
}
