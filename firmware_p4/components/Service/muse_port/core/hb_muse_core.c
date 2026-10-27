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

#include "hb_muse_core.h"

#include <string.h>

static bool usable(const hb_muse_core_t *c) {
  return c && c->valid;
}
static bool same_id(hb_muse_turn_id_t a, hb_muse_turn_id_t b) {
  return a.generation == b.generation && a.turn == b.turn;
}
static bool prerequisites(const hb_muse_core_t *c) {
  return usable(c) && !c->exhausted && c->enabled && c->paired && c->network_up &&
         c->transport_ready;
}
static void idle_state(hb_muse_core_t *c) {
  c->state = c->exhausted       ? HB_MUSE_ERROR
             : !c->enabled      ? HB_MUSE_DISABLED
             : prerequisites(c) ? HB_MUSE_READY
                                : HB_MUSE_DISCONNECTED;
}
static void forget_turn(hb_muse_core_t *c) {
  c->active = false;
  c->active_id = (hb_muse_turn_id_t){0};
  c->accepted_frames = 0;
}
static void stop_turn(hb_muse_core_t *c) {
  bool was_active = c->active;
  hb_muse_turn_id_t old_id = c->active_id;
  forget_turn(c); /* Revocation precedes even the local cancel callback. */
  idle_state(c);
  if (was_active)
    c->backend.cancel(c->user, old_id);
}
static hb_muse_result_t new_generation(hb_muse_core_t *c) {
  c->paired = c->network_up = c->transport_ready = false;
  if (c->generation == UINT32_MAX) {
    c->exhausted = true;
    c->enabled = false;
    idle_state(c);
    return HB_MUSE_ID_EXHAUSTED;
  }
  ++c->generation;
  c->next_turn = 0;
  idle_state(c);
  return HB_MUSE_OK;
}
static hb_muse_result_t backend_failed(hb_muse_core_t *c, hb_muse_io_result_t io) {
  stop_turn(c);
  c->state = HB_MUSE_ERROR;
  return io == HB_MUSE_IO_WOULD_BLOCK ? HB_MUSE_BACKPRESSURE : HB_MUSE_BACKEND_ERROR;
}

bool hb_muse_init(hb_muse_core_t *c, const hb_muse_backend_t *b, void *user) {
  if (!c)
    return false;
  memset(c, 0, sizeof(*c));
  c->state = HB_MUSE_DISABLED;
  if (!b || !b->begin || !b->audio || !b->end || !b->cancel)
    return false;
  c->backend = *b;
  c->user = user;
  c->generation = 1;
  c->valid = true;
  return true;
}
uint32_t hb_muse_generation(const hb_muse_core_t *c) {
  return usable(c) ? c->generation : 0;
}
hb_muse_state_t hb_muse_state(const hb_muse_core_t *c) {
  return usable(c) ? c->state : HB_MUSE_DISABLED;
}
bool hb_muse_active_turn(const hb_muse_core_t *c, hb_muse_turn_id_t *out) {
  if (!usable(c) || !c->active || !out)
    return false;
  *out = c->active_id;
  return true;
}
hb_muse_result_t hb_muse_set_enabled(hb_muse_core_t *c, bool enabled) {
  if (!usable(c))
    return HB_MUSE_INVALID_ARGUMENT;
  if (c->exhausted)
    return HB_MUSE_ID_EXHAUSTED;
  if (c->enabled == enabled)
    return HB_MUSE_IGNORED;
  c->enabled = enabled;
  if (!enabled) {
    stop_turn(c);
    return new_generation(c);
  }
  idle_state(c);
  return HB_MUSE_OK;
}
hb_muse_result_t hb_muse_link_update(
    hb_muse_core_t *c, uint32_t generation, bool paired, bool network_up, bool ready) {
  if (!usable(c))
    return HB_MUSE_INVALID_ARGUMENT;
  if (c->exhausted)
    return HB_MUSE_ID_EXHAUSTED;
  if (generation != c->generation)
    return HB_MUSE_STALE;
  if (ready && (!paired || !network_up)) {
    // A contradictory snapshot must never retain an older capture grant.
    c->paired = c->network_up = c->transport_ready = false;
    stop_turn(c);
    hb_muse_result_t changed = new_generation(c);
    return changed == HB_MUSE_OK ? HB_MUSE_INVALID_ARGUMENT : changed;
  }
  bool lost =
      (c->paired && !paired) || (c->network_up && !network_up) || (c->transport_ready && !ready);
  if (lost) {
    // Invalidate ALL old readiness claims, not just the falling flag.
    c->paired = c->network_up = c->transport_ready = false;
    stop_turn(c);
    return new_generation(c);
  }
  c->paired = paired;
  c->network_up = network_up;
  c->transport_ready = ready;
  if (!c->active)
    idle_state(c);
  return HB_MUSE_OK;
}
hb_muse_result_t hb_muse_connection_reset(hb_muse_core_t *c) {
  if (!usable(c))
    return HB_MUSE_INVALID_ARGUMENT;
  c->paired = c->network_up = c->transport_ready = false;
  stop_turn(c);
  return new_generation(c);
}
bool hb_muse_capture_allowed(const hb_muse_core_t *c, hb_muse_turn_id_t id) {
  return prerequisites(c) && c->active && c->ptt_held && c->state == HB_MUSE_CAPTURING &&
         same_id(id, c->active_id);
}
hb_muse_result_t hb_muse_ptt(hb_muse_core_t *c, bool held) {
  if (!usable(c))
    return HB_MUSE_INVALID_ARGUMENT;
  if (c->ptt_held == held)
    return HB_MUSE_IGNORED;
  c->ptt_held = held;
  if (!held) {
    if (!c->active || c->state != HB_MUSE_CAPTURING)
      return HB_MUSE_IGNORED;
    if (!c->accepted_frames) {
      stop_turn(c);
      return HB_MUSE_OK;
    }
    c->state = HB_MUSE_WAITING_REPLY;
    hb_muse_io_result_t io = c->backend.end(c->user, c->active_id, c->accepted_frames);
    if (io != HB_MUSE_IO_ACCEPTED)
      return backend_failed(c, io);
    return HB_MUSE_OK;
  }
  if (!prerequisites(c) || c->active)
    return HB_MUSE_NOT_READY;
  if (c->next_turn == UINT32_MAX) {
    c->exhausted = true;
    c->enabled = false;
    idle_state(c);
    return HB_MUSE_ID_EXHAUSTED;
  }
  c->active_id = (hb_muse_turn_id_t){c->generation, ++c->next_turn};
  c->accepted_frames = 0;
  c->active = true;
  // Until begin is actually accepted, capture_allowed remains false.
  c->state = HB_MUSE_READY;
  hb_muse_io_result_t io = c->backend.begin(c->user, c->active_id);
  if (io == HB_MUSE_IO_WOULD_BLOCK) {
    forget_turn(c); /* Backend guarantees no side effect for WOULD_BLOCK. */
    idle_state(c);
    return HB_MUSE_BACKPRESSURE;
  }
  if (io != HB_MUSE_IO_ACCEPTED)
    return backend_failed(c, io);
  c->state = HB_MUSE_CAPTURING;
  return HB_MUSE_OK;
}
hb_muse_result_t hb_muse_offer_pcm(hb_muse_core_t *c,
                                   hb_muse_turn_id_t id,
                                   uint32_t first_frame,
                                   const int16_t *pcm,
                                   size_t frames) {
  if (!usable(c) || !pcm || !frames || frames > HB_MUSE_PCM_MAX_FRAMES)
    return HB_MUSE_INVALID_ARGUMENT;
  if (!c->active || !same_id(id, c->active_id))
    return HB_MUSE_STALE;
  if (!hb_muse_capture_allowed(c, id))
    return HB_MUSE_NOT_READY;
  if (first_frame != c->accepted_frames)
    return HB_MUSE_OUT_OF_ORDER;
  if (frames > UINT32_MAX - c->accepted_frames) {
    stop_turn(c);
    c->state = HB_MUSE_ERROR;
    return HB_MUSE_ID_EXHAUSTED;
  }
  hb_muse_io_result_t io = c->backend.audio(c->user, id, first_frame, pcm, frames);
  if (io == HB_MUSE_IO_WOULD_BLOCK)
    return HB_MUSE_BACKPRESSURE;
  if (io != HB_MUSE_IO_ACCEPTED)
    return backend_failed(c, io);
  c->accepted_frames += (uint32_t)frames;
  return HB_MUSE_OK;
}
hb_muse_result_t hb_muse_cancel(hb_muse_core_t *c) {
  if (!usable(c))
    return HB_MUSE_INVALID_ARGUMENT;
  if (!c->active)
    return HB_MUSE_IGNORED;
  stop_turn(c);
  return HB_MUSE_OK;
}
hb_muse_result_t
hb_muse_backend_event(hb_muse_core_t *c, hb_muse_turn_id_t id, hb_muse_event_t event) {
  if (!usable(c) || (event != HB_MUSE_EVENT_TURN_DONE && event != HB_MUSE_EVENT_TURN_FAILED))
    return HB_MUSE_INVALID_ARGUMENT;
  if (!c->active || !same_id(id, c->active_id))
    return HB_MUSE_STALE;
  if (event == HB_MUSE_EVENT_TURN_FAILED)
    return backend_failed(c, HB_MUSE_IO_FAILED);
  if (c->state != HB_MUSE_WAITING_REPLY)
    return HB_MUSE_NOT_READY;
  forget_turn(c);
  idle_state(c);
  return HB_MUSE_OK;
}
