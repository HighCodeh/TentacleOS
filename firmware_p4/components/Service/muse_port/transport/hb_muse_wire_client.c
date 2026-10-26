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

#include "hb_muse_wire_client.h"
#include <string.h>

static void write64(uint8_t *p, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    p[i] = (uint8_t)(value >> (8u * i));
}
static uint64_t read64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value |= (uint64_t)p[i] << (8u * i);
  return value;
}

static void clear_pending(hb_muse_wire_client_t *c) {
  memset(&c->request, 0, sizeof(c->request));
  memset(c->encoded, 0, sizeof(c->encoded));
  c->encoded_len = 0;
  c->pending = c->sent = false;
  c->attempts = 0;
}
static void lose(hb_muse_wire_client_t *c, hb_muse_client_fault_t fault) {
  clear_pending(c);
  c->state = HB_MUSE_CLIENT_NEED_HELLO;
  c->boot = c->epoch = c->max_epoch = c->last_sequence = 0;
  c->fault = fault;
}
bool hb_muse_wire_client_init(hb_muse_wire_client_t *c, uint32_t lease) {
  if (!c)
    return false;
  memset(c, 0, sizeof(*c));
  c->lease_ms = lease ? lease : HB_MUSE_WIRE_DEFAULT_LEASE_MS;
  if (c->lease_ms <= HB_MUSE_CLIENT_REQUEST_MS)
    return false;
  c->initialized = true;
  return true;
}
void hb_muse_wire_client_tick(hb_muse_wire_client_t *c, uint64_t now) {
  if (!c || !c->initialized || c->clock_fault)
    return;
  if (c->clock_started && now < c->clock_ms) {
    c->clock_fault = true;
    lose(c, HB_MUSE_CLIENT_FAULT_CLOCK);
    return;
  }
  c->clock_started = true;
  c->clock_ms = now;
  if (c->state == HB_MUSE_CLIENT_ACTIVE && now - c->lease_start_ms >= c->lease_ms) {
    lose(c, HB_MUSE_CLIENT_FAULT_LEASE);
  } else if (c->pending && c->sent && now - c->first_send_ms >= HB_MUSE_CLIENT_REQUEST_MS) {
    lose(c, HB_MUSE_CLIENT_FAULT_TIMEOUT);
  }
}
static bool available(hb_muse_wire_client_t *c, uint64_t now) {
  hb_muse_wire_client_tick(c, now);
  return c && c->initialized && !c->clock_fault && !c->pending;
}
static bool prepare(hb_muse_wire_client_t *c, const hb_muse_wire_frame_t *request) {
  if (!hb_muse_wire_encode(request, c->encoded, sizeof(c->encoded), &c->encoded_len))
    return false;
  c->request = *request;
  c->pending = true;
  c->sent = false;
  c->attempts = 0;
  return true;
}
bool hb_muse_wire_client_prepare_hello(hb_muse_wire_client_t *c, uint64_t nonce, uint64_t now) {
  if (!available(c, now) || c->state == HB_MUSE_CLIENT_ACTIVE || !nonce ||
      nonce == c->discovery_nonce)
    return false;
  hb_muse_wire_frame_t request = {0};
  request.opcode = HB_MUSE_WIRE_HELLO;
  request.payload_len = HB_MUSE_WIRE_NONCE_BYTES;
  write64(request.payload, nonce);
  if (!prepare(c, &request))
    return false;
  c->discovery_nonce = nonce;
  return true;
}
bool hb_muse_wire_client_prepare_open(hb_muse_wire_client_t *c, uint64_t now) {
  if (!available(c, now) || c->state != HB_MUSE_CLIENT_CAN_OPEN)
    return false;
  if (c->max_epoch >= UINT32_MAX - 1u) {
    lose(c, HB_MUSE_CLIENT_FAULT_EXHAUSTED);
    return false;
  }
  hb_muse_wire_frame_t request = {0};
  request.opcode = HB_MUSE_WIRE_OPEN;
  request.boot = c->boot;
  request.epoch = c->max_epoch + 1u;
  request.sequence = 1;
  request.payload_len = HB_MUSE_WIRE_OPEN_BYTES;
  write64(request.payload, c->discovery_nonce);
  return prepare(c, &request);
}
bool hb_muse_wire_client_prepare_command(hb_muse_wire_client_t *c,
                                         uint8_t opcode,
                                         uint32_t generation,
                                         uint32_t turn,
                                         const uint8_t *payload,
                                         size_t length,
                                         uint64_t now) {
  if (!available(c, now) || c->state != HB_MUSE_CLIENT_ACTIVE || opcode < HB_MUSE_WIRE_BEGIN ||
      opcode > HB_MUSE_WIRE_CLOSE || length > HB_MUSE_WIRE_PAYLOAD_MAX || (length && !payload) ||
      (opcode == HB_MUSE_WIRE_CLOSE && (generation || turn || length)))
    return false;
  if (c->last_sequence == UINT32_MAX) {
    lose(c, HB_MUSE_CLIENT_FAULT_EXHAUSTED);
    return false;
  }
  hb_muse_wire_frame_t request = {0};
  request.opcode = opcode;
  request.boot = c->boot;
  request.epoch = c->epoch;
  request.sequence = c->last_sequence + 1u;
  request.generation = generation;
  request.turn = turn;
  request.payload_len = (uint16_t)length;
  if (length)
    memcpy(request.payload, payload, length);
  return prepare(c, &request);
}
bool hb_muse_wire_client_next_request(
    hb_muse_wire_client_t *c, uint64_t now, uint8_t *bytes, size_t capacity, size_t *length) {
  if (length)
    *length = 0;
  hb_muse_wire_client_tick(c, now);
  if (!c || !c->initialized || c->clock_fault || !c->pending || !bytes || !length ||
      capacity < c->encoded_len || c->attempts >= HB_MUSE_CLIENT_ATTEMPTS ||
      (c->sent && now - c->last_send_ms < HB_MUSE_CLIENT_RETRY_MS))
    return false;
  memcpy(bytes, c->encoded, c->encoded_len);
  *length = c->encoded_len;
  if (!c->sent)
    c->first_send_ms = now;
  c->last_send_ms = now;
  c->sent = true;
  ++c->attempts;
  return true;
}
static bool correlated(const hb_muse_wire_frame_t *a, const hb_muse_wire_frame_t *b) {
  return a->opcode == b->opcode && a->epoch == b->epoch && a->sequence == b->sequence &&
         a->generation == b->generation && a->turn == b->turn;
}
static uint32_t read32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
hb_muse_client_accept_t hb_muse_wire_client_accept_response(hb_muse_wire_client_t *c,
                                                            const uint8_t *bytes,
                                                            size_t length,
                                                            uint64_t now,
                                                            hb_muse_wire_frame_t *out) {
  hb_muse_wire_client_tick(c, now);
  if (!c || !c->initialized || c->clock_fault || !c->pending || !c->sent || !out)
    return HB_MUSE_CLIENT_IGNORED;
  hb_muse_wire_frame_t reply;
  if (!hb_muse_wire_decode(bytes, length, &reply) || reply.flags != HB_MUSE_WIRE_RESPONSE ||
      !correlated(&reply, &c->request))
    return HB_MUSE_CLIENT_IGNORED;
  if (reply.opcode == HB_MUSE_WIRE_HELLO) {
    if (reply.status != HB_MUSE_WIRE_OK || !reply.boot ||
        reply.payload_len != HB_MUSE_WIRE_HELLO_BYTES || reply.payload[4] > 1u ||
        read64(reply.payload + 5) != c->discovery_nonce)
      return HB_MUSE_CLIENT_IGNORED;
    c->boot = reply.boot;
    c->max_epoch = read32(reply.payload);
    c->state = reply.payload[4] ? HB_MUSE_CLIENT_PEER_BUSY : HB_MUSE_CLIENT_CAN_OPEN;
    c->fault = HB_MUSE_CLIENT_FAULT_NONE;
  } else {
    if (reply.opcode == HB_MUSE_WIRE_OPEN && (reply.payload_len != HB_MUSE_WIRE_OPEN_BYTES ||
                                              read64(reply.payload) != c->discovery_nonce))
      return HB_MUSE_CLIENT_IGNORED;
    if (reply.boot != c->boot) {
      if (reply.status != HB_MUSE_WIRE_STALE)
        return HB_MUSE_CLIENT_IGNORED;
      lose(c, HB_MUSE_CLIENT_FAULT_REBOOT);
      return HB_MUSE_CLIENT_LINK_LOST;
    }
    if (reply.status == HB_MUSE_WIRE_GAP || reply.status == HB_MUSE_WIRE_CONFLICT ||
        reply.status == HB_MUSE_WIRE_STALE) {
      lose(c, HB_MUSE_CLIENT_FAULT_PROTOCOL);
      return HB_MUSE_CLIENT_LINK_LOST;
    }
    if (reply.opcode == HB_MUSE_WIRE_CLOSE && reply.payload_len)
      return HB_MUSE_CLIENT_IGNORED;
    if (reply.opcode == HB_MUSE_WIRE_OPEN) {
      if (reply.status == HB_MUSE_WIRE_OK) {
        c->state = HB_MUSE_CLIENT_ACTIVE;
        c->epoch = c->max_epoch = reply.epoch;
        c->last_sequence = 1;
        c->lease_start_ms = c->first_send_ms;
        c->fault = HB_MUSE_CLIENT_FAULT_NONE;
      } else if (reply.status == HB_MUSE_WIRE_BUSY) {
        c->state = HB_MUSE_CLIENT_PEER_BUSY;
      } else {
        lose(c, HB_MUSE_CLIENT_FAULT_PROTOCOL);
      }
    } else if (reply.status == HB_MUSE_WIRE_REJECTED) {
      lose(c, HB_MUSE_CLIENT_FAULT_PROTOCOL);
    } else if (reply.opcode == HB_MUSE_WIRE_CLOSE && reply.status == HB_MUSE_WIRE_OK) {
      lose(c, HB_MUSE_CLIENT_FAULT_NONE);
    } else {
      c->last_sequence = reply.sequence;
      c->lease_start_ms = c->first_send_ms;
    }
  }
  clear_pending(c);
  *out = reply;
  return HB_MUSE_CLIENT_RESPONSE;
}
void hb_muse_wire_client_abandon(hb_muse_wire_client_t *c) {
  if (!c || !c->initialized || c->clock_fault)
    return;
  lose(c, HB_MUSE_CLIENT_FAULT_ABANDONED);
}
