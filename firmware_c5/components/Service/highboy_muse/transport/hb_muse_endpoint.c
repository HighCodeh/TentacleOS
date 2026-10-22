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

#include "hb_muse_endpoint.h"
#include <string.h>

static uint32_t ep_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t ep_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static void ep_put32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(v >> (i * 8));
}
static void ep_put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static bool ep_equal(hb_muse_turn_id_t a, hb_muse_turn_id_t b) {
  return a.generation == b.generation && a.turn == b.turn;
}
static bool ep_newer(hb_muse_turn_id_t a, hb_muse_turn_id_t b) {
  return a.generation && a.turn &&
         (a.generation > b.generation || (a.generation == b.generation && a.turn > b.turn));
}
static bool ep_quiet(void *user) {
  hb_muse_endpoint_t *e = user;
  return e->initialized && !e->binding.clock_fault &&
         e->binding.adapter.phase == HB_MUSE_ADAPTER_IDLE && !e->binding.owns_sdk;
}
static bool ep_permitted(const hb_muse_endpoint_t *e) {
  return e->enabled && e->paired && e->network && hb_muse_sdk_binding_connected(&e->binding);
}
static void ep_revoke(void *user) {
  hb_muse_endpoint_t *e = user;
  if (!e->active || e->phase == HB_MUSE_ENDPOINT_DONE || e->phase == HB_MUSE_ENDPOINT_CANCELLED)
    return;
  e->cancelling = true;
  hb_muse_pcm_adapter_backend()->cancel(&e->binding.adapter, e->sdk_id);
  e->phase = e->terminal_failed ? HB_MUSE_ENDPOINT_FAILED : HB_MUSE_ENDPOINT_CANCELLING;
}
static void ep_clear_turn(hb_muse_endpoint_t *e) {
  e->external_id = (hb_muse_turn_id_t){0};
  e->sdk_id = (hb_muse_turn_id_t){0};
  e->accepted = e->consumed = e->text_revision = e->message_index = 0;
  e->text_bytes = 0;
  memset(e->text, 0, sizeof(e->text));
  e->active = e->cancelling = e->begin_ack = e->end_ack = e->note_ack = false;
  e->text_truncated = e->terminal_failed = false;
  e->phase = HB_MUSE_ENDPOINT_IDLE;
}
static void ep_opened(void *user, uint32_t epoch) {
  hb_muse_endpoint_t *e = user;
  /* Called only after the server verifies ep_quiet, never on replay. */
  ep_clear_turn(e);
  e->epoch = epoch;
  e->local_turn = 0;
  e->last_external_id = (hb_muse_turn_id_t){0};
}
static void ep_status(const hb_muse_endpoint_t *e, uint8_t *reply, uint16_t *length) {
  memset(reply, 0, HB_MUSE_ENDPOINT_STATUS_BYTES);
  reply[0] = (uint8_t)e->phase;
  reply[1] =
      (uint8_t)((e->enabled ? 1 : 0) | (e->paired ? 2 : 0) | (e->network ? 4 : 0) |
                (hb_muse_sdk_binding_connected(&e->binding) ? 8 : 0) | (e->begin_ack ? 16 : 0) |
                (e->end_ack ? 32 : 0) | (e->note_ack ? 64 : 0) |
                ((e->binding.adapter.phase == HB_MUSE_ADAPTER_IDLE && !e->binding.owns_sdk) ? 128
                                                                                            : 0));
  reply[3] = e->text_truncated ? 1 : 0;
  ep_put32(reply + 4, e->accepted);
  ep_put32(reply + 8, e->consumed);
  ep_put32(reply + 12, e->text_revision);
  ep_put32(reply + 16, e->message_index);
  ep_put16(reply + 20, e->text_bytes);
  ep_put16(reply + 22, (uint16_t)(HB_MUSE_ADAPTER_CAPACITY - e->binding.adapter.queued));
  ep_put32(reply + 24, HB_MUSE_SDK_MAX_FRAMES);
  ep_put16(reply + 28, HB_MUSE_ENDPOINT_PCM_FRAMES);
  ep_put16(reply + 30, HB_MUSE_PCM_RATE_HZ);
  *length = HB_MUSE_ENDPOINT_STATUS_BYTES;
}
static hb_muse_wire_status_t ep_result(hb_muse_io_result_t r) {
  return r == HB_MUSE_IO_ACCEPTED      ? HB_MUSE_WIRE_OK
         : r == HB_MUSE_IO_WOULD_BLOCK ? HB_MUSE_WIRE_BLOCKED
                                       : HB_MUSE_WIRE_REJECTED;
}
static hb_muse_wire_status_t
ep_dispatch(void *user, const hb_muse_wire_frame_t *r, uint8_t *reply, uint16_t *length) {
  hb_muse_endpoint_t *e = user;
  const hb_muse_backend_t *backend = hb_muse_pcm_adapter_backend();
  hb_muse_turn_id_t id = {r->generation, r->turn};
  *length = 0;
  if (r->epoch != e->epoch)
    return HB_MUSE_WIRE_REJECTED;
  if (r->opcode == HB_MUSE_WIRE_BEGIN) {
    if (r->payload_len || !ep_newer(id, e->last_external_id) || e->local_turn == UINT32_MAX)
      return HB_MUSE_WIRE_REJECTED;
    if (e->active || !ep_quiet(e))
      return HB_MUSE_WIRE_BUSY;
    if (!ep_permitted(e))
      return HB_MUSE_WIRE_BLOCKED;
    hb_muse_turn_id_t local = {e->epoch, e->local_turn + 1};
    hb_muse_io_result_t result = backend->begin(&e->binding.adapter, local);
    if (result != HB_MUSE_IO_ACCEPTED)
      return ep_result(result);
    ep_clear_turn(e);
    ++e->local_turn;
    e->external_id = e->last_external_id = id;
    e->sdk_id = local;
    e->active = true;
    e->phase = HB_MUSE_ENDPOINT_STARTING;
    return HB_MUSE_WIRE_OK;
  }
  if (r->opcode == HB_MUSE_WIRE_STATUS) {
    if (r->payload_len || !ep_equal(id, e->external_id))
      return HB_MUSE_WIRE_REJECTED;
    ep_status(e, reply, length);
    return HB_MUSE_WIRE_OK;
  }
  if (!e->active || !ep_equal(id, e->external_id))
    return HB_MUSE_WIRE_REJECTED;
  switch (r->opcode) {
    case HB_MUSE_WIRE_PCM: {
      if (r->payload_len < 6 || ((r->payload_len - 4) & 1u) ||
          r->payload_len > 4 + 2 * HB_MUSE_ENDPOINT_PCM_FRAMES ||
          ep_u32(r->payload) != e->accepted || e->cancelling ||
          e->phase != HB_MUSE_ENDPOINT_LISTENING)
        return HB_MUSE_WIRE_REJECTED;
      size_t count = ((size_t)r->payload_len - 4u) / 2u;
      if (!ep_permitted(e)) {
        ep_revoke(e);
        return HB_MUSE_WIRE_REJECTED;
      }
      if (count > HB_MUSE_SDK_MAX_FRAMES - e->accepted) {
        e->terminal_failed = true;
        ep_revoke(e);
        return HB_MUSE_WIRE_REJECTED;
      }
      if (!hb_muse_sdk_binding_capture_ready(&e->binding))
        return HB_MUSE_WIRE_BLOCKED;
      int16_t pcm[HB_MUSE_ENDPOINT_PCM_FRAMES];
      for (size_t i = 0; i < count; ++i) {
        uint16_t raw = ep_u16(r->payload + 4 + i * 2);
        pcm[i] = (int16_t)(raw < 0x8000u ? (int32_t)raw : (int32_t)raw - 65536);
      }
      hb_muse_io_result_t result =
          backend->audio(&e->binding.adapter, e->sdk_id, e->accepted, pcm, count);
      memset(pcm, 0, sizeof(pcm));
      if (result == HB_MUSE_IO_ACCEPTED)
        e->accepted += (uint32_t)count;
      else if (result == HB_MUSE_IO_FAILED) {
        e->terminal_failed = true;
        ep_revoke(e);
      }
      return ep_result(result);
    }
    case HB_MUSE_WIRE_END: {
      if (r->payload_len != 4 || !e->accepted || ep_u32(r->payload) != e->accepted ||
          e->phase != HB_MUSE_ENDPOINT_LISTENING || e->cancelling)
        return HB_MUSE_WIRE_REJECTED;
      hb_muse_io_result_t result = backend->end(&e->binding.adapter, e->sdk_id, e->accepted);
      if (result == HB_MUSE_IO_ACCEPTED)
        e->phase = HB_MUSE_ENDPOINT_FINISHING;
      else if (result == HB_MUSE_IO_FAILED) {
        e->terminal_failed = true;
        ep_revoke(e);
      }
      return ep_result(result);
    }
    case HB_MUSE_WIRE_CANCEL:
      if (r->payload_len)
        return HB_MUSE_WIRE_REJECTED;
      ep_revoke(e);
      return HB_MUSE_WIRE_OK; /* Cancellation requested, not yet fenced. */
    case HB_MUSE_WIRE_TEXT: {
      if (r->payload_len != 6)
        return HB_MUSE_WIRE_REJECTED;
      if (ep_u32(r->payload) != e->text_revision)
        return HB_MUSE_WIRE_BLOCKED;
      uint16_t offset = ep_u16(r->payload + 4);
      if (offset > e->text_bytes)
        return HB_MUSE_WIRE_REJECTED;
      size_t count = e->text_bytes - offset;
      if (count > HB_MUSE_WIRE_PAYLOAD_MAX - HB_MUSE_ENDPOINT_TEXT_HEADER)
        count = HB_MUSE_WIRE_PAYLOAD_MAX - HB_MUSE_ENDPOINT_TEXT_HEADER;
      ep_put32(reply, e->text_revision);
      ep_put32(reply + 4, e->message_index);
      ep_put16(reply + 8, e->text_bytes);
      ep_put16(reply + 10, offset);
      memcpy(reply + HB_MUSE_ENDPOINT_TEXT_HEADER, e->text + offset, count);
      *length = (uint16_t)(HB_MUSE_ENDPOINT_TEXT_HEADER + count);
      return HB_MUSE_WIRE_OK;
    }
    case HB_MUSE_WIRE_RETIRE:
      if (r->payload_len)
        return HB_MUSE_WIRE_REJECTED;
      if (!ep_quiet(e) ||
          (e->phase != HB_MUSE_ENDPOINT_DONE && e->phase != HB_MUSE_ENDPOINT_FAILED &&
           e->phase != HB_MUSE_ENDPOINT_CANCELLED))
        return HB_MUSE_WIRE_BUSY;
      ep_clear_turn(e);
      return HB_MUSE_WIRE_OK;
    default:
      return HB_MUSE_WIRE_REJECTED;
  }
}
bool hb_muse_endpoint_init(hb_muse_endpoint_t *e, uint32_t boot) {
  if (!e || !boot)
    return false;
  memset(e, 0, sizeof(*e));
  if (!hb_muse_sdk_binding_init(&e->binding))
    return false;
  const hb_muse_wire_callbacks_t callbacks = {
      .can_open = ep_quiet, .dispatch = ep_dispatch, .revoke = ep_revoke, .opened = ep_opened};
  e->initialized = hb_muse_wire_server_init(&e->server, boot, 0, &callbacks, e);
  return e->initialized;
}
void hb_muse_endpoint_access(hb_muse_endpoint_t *e, bool enabled, bool paired, bool network) {
  if (!e || !e->initialized)
    return;
  e->enabled = enabled;
  e->paired = paired;
  e->network = network;
  if (!enabled || !paired || !network)
    ep_revoke(e);
}
void hb_muse_endpoint_pump(hb_muse_endpoint_t *e, uint64_t now) {
  if (!e || !e->initialized)
    return;
  hb_muse_wire_server_expire(&e->server, now);
  if (!e->enabled || !e->paired || !e->network)
    ep_revoke(e);
  hb_muse_sdk_binding_pump(&e->binding, now);
  if (!e->active)
    return;
  hb_muse_pcm_adapter_t *a = &e->binding.adapter;
  const hb_muse_sdk_snapshot_t *s = &e->binding.snapshot;
  if (e->binding.snapshot_valid && s->id.generation == e->sdk_id.generation &&
      s->id.turn == e->sdk_id.turn) {
    e->begin_ack |= s->begin_ack;
    e->end_ack |= s->end_ack;
    e->note_ack |= s->note_ack;
    if (s->accepted_frames > e->consumed && s->accepted_frames <= e->accepted)
      e->consumed = s->accepted_frames;
    /* Capture before a subsequent pump observes SDK retire/zeroed snapshot. */
    if (s->text_revision && s->text_revision != e->text_revision) {
      size_t size = 0;
      while (size + 1 < sizeof(e->text) && s->text[size])
        ++size;
      memset(e->text, 0, sizeof(e->text));
      memcpy(e->text, s->text, size);
      e->text_bytes = (uint16_t)size;
      e->text_revision = s->text_revision;
      e->message_index = s->message_index;
      e->text_truncated = s->text_truncated;
    }
  }
  if (ep_equal(a->id, e->sdk_id) && a->consumed_frames > e->consumed)
    e->consumed = a->consumed_frames;
  hb_muse_turn_id_t event_id;
  hb_muse_event_t event;
  if (hb_muse_pcm_adapter_take_event(a, &event_id, &event) && ep_equal(event_id, e->sdk_id)) {
    if (event == HB_MUSE_EVENT_TURN_DONE)
      e->phase = HB_MUSE_ENDPOINT_DONE;
    else {
      e->terminal_failed = true;
      e->phase = HB_MUSE_ENDPOINT_FAILED;
    }
  }
  if (e->terminal_failed) {
    e->phase = HB_MUSE_ENDPOINT_FAILED;
    return;
  }
  if (e->cancelling) {
    e->phase = ep_quiet(e) ? HB_MUSE_ENDPOINT_CANCELLED : HB_MUSE_ENDPOINT_CANCELLING;
    return;
  }
  if (e->phase == HB_MUSE_ENDPOINT_DONE)
    return;
  if (a->phase == HB_MUSE_ADAPTER_REPLY_WAIT)
    e->phase = HB_MUSE_ENDPOINT_WAITING;
  else if (a->phase == HB_MUSE_ADAPTER_END_WAIT || a->end_requested)
    e->phase = HB_MUSE_ENDPOINT_FINISHING;
  else if (hb_muse_sdk_binding_capture_ready(&e->binding))
    e->phase = HB_MUSE_ENDPOINT_LISTENING;
}
bool hb_muse_endpoint_process(hb_muse_endpoint_t *e,
                              const uint8_t *request,
                              size_t request_len,
                              uint64_t now,
                              uint8_t *response,
                              size_t capacity,
                              size_t *response_len) {
  if (!e || !e->initialized)
    return false;
  return hb_muse_wire_server_process(
      &e->server, request, request_len, now, response, capacity, response_len);
}
