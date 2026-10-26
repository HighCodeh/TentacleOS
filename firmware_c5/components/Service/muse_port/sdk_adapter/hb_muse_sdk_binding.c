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

#include "hb_muse_sdk_binding.h"
#include <string.h>

static hb_muse_sdk_id_t sdk_id(hb_muse_turn_id_t id) {
  hb_muse_sdk_id_t out = {id.generation, id.turn};
  return out;
}
static bool binding_same_id(hb_muse_sdk_id_t a, hb_muse_turn_id_t b) {
  return a.generation == b.generation && a.turn == b.turn;
}
static hb_muse_dispatch_t binding_dispatch(hb_muse_sdk_dispatch_t result) {
  return result == HB_MUSE_SDK_QUEUED    ? HB_MUSE_DISPATCH_QUEUED
         : result == HB_MUSE_SDK_BLOCKED ? HB_MUSE_DISPATCH_BLOCKED
                                         : HB_MUSE_DISPATCH_FAILED;
}
static hb_muse_dispatch_t binding_begin(void *user, hb_muse_turn_id_t id) {
  hb_muse_sdk_binding_t *b = user;
  hb_muse_sdk_dispatch_t result = hb_muse_sdk_begin(sdk_id(id));
  if (result == HB_MUSE_SDK_QUEUED)
    b->owns_sdk = true;
  return binding_dispatch(result);
}
static hb_muse_audio_write_t
binding_audio(void *user, hb_muse_turn_id_t id, uint32_t first, const int16_t *pcm, size_t frames) {
  (void)user;
  hb_muse_sdk_audio_t result = hb_muse_sdk_audio(sdk_id(id), first, pcm, frames);
  hb_muse_audio_write_t out = {result.failed   ? HB_MUSE_AUDIO_FAILED
                               : result.frames ? HB_MUSE_AUDIO_PROGRESS
                                               : HB_MUSE_AUDIO_BLOCKED,
                               result.failed ? 0 : result.frames};
  return out;
}
static hb_muse_dispatch_t binding_end(void *user, hb_muse_turn_id_t id, uint32_t total) {
  (void)user;
  return binding_dispatch(hb_muse_sdk_end(sdk_id(id), total));
}
static hb_muse_dispatch_t binding_cancel(void *user, hb_muse_turn_id_t id) {
  hb_muse_sdk_binding_t *b = user;
  /* No SDK command ever entered the queue: only a local operation exists. */
  return b->owns_sdk ? binding_dispatch(hb_muse_sdk_cancel(sdk_id(id))) : HB_MUSE_DISPATCH_QUEUED;
}
static bool binding_cancelling(hb_muse_adapter_phase_t phase) {
  return phase == HB_MUSE_ADAPTER_CANCEL_PENDING || phase == HB_MUSE_ADAPTER_CANCEL_WAIT ||
         phase == HB_MUSE_ADAPTER_FENCE_REQUIRED;
}
bool hb_muse_sdk_binding_init(hb_muse_sdk_binding_t *b) {
  static const hb_muse_adapter_transport_t transport = {
      binding_begin, binding_audio, binding_end, binding_cancel};
  if (!b)
    return false;
  memset(b, 0, sizeof(*b));
  b->initialized = hb_muse_pcm_adapter_init(&b->adapter, &transport, b);
  return b->initialized;
}
static void binding_observe(hb_muse_sdk_binding_t *b) {
  hb_muse_pcm_adapter_t *a = &b->adapter;
  if (a->id.generation != b->observed_id.generation || a->id.turn != b->observed_id.turn) {
    b->observed_id = a->id;
    b->turn_ms = b->phase_ms = b->progress_ms = b->now_ms;
    b->progress_frames = a->consumed_frames;
    b->audio_waiting = false;
    b->observed_operation = 0;
  }
  if (a->phase != b->observed_phase) {
    b->observed_phase = a->phase;
    b->phase_ms = b->progress_ms = b->now_ms;
  }
  if (a->queued && !b->audio_waiting)
    b->progress_ms = b->now_ms;
  b->audio_waiting = a->queued != 0;
  if (a->consumed_frames != b->progress_frames) {
    b->progress_frames = a->consumed_frames;
    b->progress_ms = b->now_ms;
  }
  unsigned operation =
      binding_cancelling(a->phase)                                                            ? 3u
      : (a->phase == HB_MUSE_ADAPTER_BEGIN_PENDING || a->phase == HB_MUSE_ADAPTER_BEGIN_WAIT) ? 1u
      : (a->phase == HB_MUSE_ADAPTER_END_WAIT ||
         (a->phase == HB_MUSE_ADAPTER_STREAM && a->end_requested && !a->queued))
          ? 2u
          : 0u;
  if (operation != b->observed_operation) {
    b->observed_operation = operation;
    b->operation_ms = b->now_ms;
  }
}
void hb_muse_sdk_binding_pump(hb_muse_sdk_binding_t *b, uint64_t now) {
  if (!b || !b->initialized)
    return;
  hb_muse_pcm_adapter_t *a = &b->adapter;
  if (b->clock_started && now < b->now_ms)
    b->clock_fault = true;
  b->clock_started = true;
  if (!b->clock_fault)
    b->now_ms = now;
  binding_observe(b);
  bool snapshot_ok = hb_muse_sdk_snapshot(&b->snapshot);
  b->snapshot_valid = snapshot_ok;
  /* Evaluate the existing wait BEFORE an ACK can advance phase/reset clocks.
   * An ACK first observed after the deadline cannot revive expired capture. */
  if (a->phase != HB_MUSE_ADAPTER_IDLE) {
    bool stalled = (b->observed_operation && b->now_ms - b->operation_ms >= 20000u) ||
                   (a->queued && b->now_ms - b->progress_ms >= 1000u) ||
                   b->now_ms - b->turn_ms >= 180000u;
    if (stalled)
      b->timed_out = true;
    if (!snapshot_ok || b->clock_fault || stalled)
      (void)hb_muse_pcm_adapter_event(a, a->id, HB_MUSE_ADAPTER_TURN_FAILED);
  }
  if (snapshot_ok && b->owns_sdk && binding_same_id(b->snapshot.id, a->id)) {
    if (b->snapshot.phase == HB_MUSE_SDK_FAILED)
      (void)hb_muse_pcm_adapter_event(a, a->id, HB_MUSE_ADAPTER_TURN_FAILED);
    if (b->snapshot.begin_ack)
      (void)hb_muse_pcm_adapter_event(a, a->id, HB_MUSE_ADAPTER_BEGIN_ACK);
    if (b->snapshot.end_ack)
      (void)hb_muse_pcm_adapter_event(a, a->id, HB_MUSE_ADAPTER_END_ACK);
    if (b->snapshot.phase == HB_MUSE_SDK_DONE && a->phase == HB_MUSE_ADAPTER_REPLY_WAIT) {
      hb_muse_turn_id_t id = a->id;
      if (hb_muse_sdk_retire(sdk_id(id))) {
        b->owns_sdk = false;
        (void)hb_muse_pcm_adapter_event(a, id, HB_MUSE_ADAPTER_TURN_DONE);
      } else
        (void)hb_muse_pcm_adapter_event(a, id, HB_MUSE_ADAPTER_TURN_FAILED);
    } else if (b->snapshot.phase == HB_MUSE_SDK_FENCED && binding_cancelling(a->phase)) {
      hb_muse_turn_id_t id = a->id;
      if (hb_muse_sdk_retire(sdk_id(id))) {
        b->owns_sdk = false;
        (void)hb_muse_pcm_adapter_event(a, id, HB_MUSE_ADAPTER_FENCED);
      }
    }
  }
  if (!b->owns_sdk && binding_cancelling(a->phase))
    (void)hb_muse_pcm_adapter_event(a, a->id, HB_MUSE_ADAPTER_FENCED);
  binding_observe(b);
  /* Clock failure cannot begin a turn; cancellation may still drain safely. */
  if (!b->clock_fault || binding_cancelling(a->phase))
    hb_muse_pcm_adapter_pump(a);
  binding_observe(b);
}
bool hb_muse_sdk_binding_connected(const hb_muse_sdk_binding_t *b) {
  return b && b->initialized && b->snapshot_valid && !b->clock_fault &&
         b->snapshot.worker_started && b->snapshot.session_ready;
}
bool hb_muse_sdk_binding_capture_ready(const hb_muse_sdk_binding_t *b) {
  return hb_muse_sdk_binding_connected(b) && b->owns_sdk &&
         b->adapter.phase == HB_MUSE_ADAPTER_STREAM && !b->adapter.end_requested &&
         binding_same_id(b->snapshot.id, b->adapter.id) &&
         b->snapshot.phase == HB_MUSE_SDK_LISTENING && b->snapshot.begin_ack;
}
