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

#include "hb_muse_pcm_adapter.h"

#include <string.h>

static bool usable(const hb_muse_pcm_adapter_t *a) {
  return a && a->valid;
}
static bool equal_id(hb_muse_turn_id_t a, hb_muse_turn_id_t b) {
  return a.generation == b.generation && a.turn == b.turn;
}
static bool newer_id(hb_muse_turn_id_t a, hb_muse_turn_id_t b) {
  return a.generation && a.turn &&
         (a.generation > b.generation || (a.generation == b.generation && a.turn > b.turn));
}
static bool cancelling(const hb_muse_pcm_adapter_t *a) {
  return a->phase == HB_MUSE_ADAPTER_CANCEL_PENDING || a->phase == HB_MUSE_ADAPTER_CANCEL_WAIT ||
         a->phase == HB_MUSE_ADAPTER_FENCE_REQUIRED;
}
static void erase_pcm(hb_muse_pcm_adapter_t *a) {
  memset(a->pcm, 0, sizeof(a->pcm));
  a->head = a->queued = 0;
}
static void retire(hb_muse_pcm_adapter_t *a) {
  erase_pcm(a);
  a->phase = HB_MUSE_ADAPTER_IDLE;
  a->id = (hb_muse_turn_id_t){0};
  a->accepted_frames = a->consumed_frames = 0;
  a->end_requested = a->transport_touched = false;
}
static void revoke(hb_muse_pcm_adapter_t *a) {
  erase_pcm(a); /* Local revocation always precedes any remote cancel. */
  a->end_requested = true;
  if (cancelling(a))
    return;
  if (a->transport_touched)
    a->phase = HB_MUSE_ADAPTER_CANCEL_PENDING;
  else
    retire(a);
}
static void failed(hb_muse_pcm_adapter_t *a) {
  a->event_id = a->id;
  a->pending_event = HB_MUSE_EVENT_TURN_FAILED;
  a->event_pending = true;
  revoke(a);
}

static hb_muse_io_result_t begin(void *user, hb_muse_turn_id_t id) {
  hb_muse_pcm_adapter_t *a = user;
  if (!usable(a) || !newer_id(id, a->last_id))
    return HB_MUSE_IO_FAILED;
  if (a->phase != HB_MUSE_ADAPTER_IDLE || a->event_pending)
    return HB_MUSE_IO_WOULD_BLOCK;
  a->id = a->last_id = id;
  a->phase = HB_MUSE_ADAPTER_BEGIN_PENDING;
  return HB_MUSE_IO_ACCEPTED; /* Local operation accepted, no SDK call. */
}
static hb_muse_io_result_t
audio(void *user, hb_muse_turn_id_t id, uint32_t first, const int16_t *pcm, size_t frames) {
  hb_muse_pcm_adapter_t *a = user;
  if (!usable(a) || !equal_id(id, a->id) || !pcm || !frames || frames > HB_MUSE_PCM_MAX_FRAMES ||
      a->end_requested ||
      (a->phase != HB_MUSE_ADAPTER_BEGIN_PENDING && a->phase != HB_MUSE_ADAPTER_BEGIN_WAIT &&
       a->phase != HB_MUSE_ADAPTER_STREAM) ||
      first != a->accepted_frames || frames > UINT32_MAX - first)
    return HB_MUSE_IO_FAILED;
  if (frames > HB_MUSE_ADAPTER_CAPACITY - a->queued)
    return HB_MUSE_IO_WOULD_BLOCK;
  size_t tail = (a->head + a->queued) % HB_MUSE_ADAPTER_CAPACITY;
  size_t n = HB_MUSE_ADAPTER_CAPACITY - tail;
  if (n > frames)
    n = frames;
  memcpy(a->pcm + tail, pcm, n * sizeof(*pcm));
  memcpy(a->pcm, pcm + n, (frames - n) * sizeof(*pcm));
  a->queued += frames;
  a->accepted_frames += (uint32_t)frames;
  return HB_MUSE_IO_ACCEPTED;
}
static hb_muse_io_result_t end(void *user, hb_muse_turn_id_t id, uint32_t total) {
  hb_muse_pcm_adapter_t *a = user;
  if (!usable(a) || !equal_id(id, a->id) || a->end_requested || !total ||
      total != a->accepted_frames ||
      (a->phase != HB_MUSE_ADAPTER_BEGIN_PENDING && a->phase != HB_MUSE_ADAPTER_BEGIN_WAIT &&
       a->phase != HB_MUSE_ADAPTER_STREAM))
    return HB_MUSE_IO_FAILED;
  a->end_requested = true;
  return HB_MUSE_IO_ACCEPTED; /* Finish only after the accepted ring drains. */
}
static void cancel(void *user, hb_muse_turn_id_t id) {
  hb_muse_pcm_adapter_t *a = user;
  if (!usable(a) || a->phase == HB_MUSE_ADAPTER_IDLE || !equal_id(id, a->id))
    return;
  revoke(a);
}
static const hb_muse_backend_t BACKEND = {begin, audio, end, cancel};
const hb_muse_backend_t *hb_muse_pcm_adapter_backend(void) {
  return &BACKEND;
}

bool hb_muse_pcm_adapter_init(hb_muse_pcm_adapter_t *a,
                              const hb_muse_adapter_transport_t *transport,
                              void *user) {
  if (!a)
    return false;
  memset(a, 0, sizeof(*a));
  if (!transport || !transport->begin || !transport->audio || !transport->end || !transport->cancel)
    return false;
  a->transport = *transport;
  a->user = user;
  a->valid = true;
  return true;
}

void hb_muse_pcm_adapter_pump(hb_muse_pcm_adapter_t *a) {
  if (!usable(a))
    return;
  hb_muse_dispatch_t dispatch;
  switch (a->phase) {
    case HB_MUSE_ADAPTER_BEGIN_PENDING:
      dispatch = a->transport.begin(a->user, a->id);
      if (dispatch == HB_MUSE_DISPATCH_BLOCKED)
        return;
      a->transport_touched = true; /* Even FAILED may have side effects. */
      if (dispatch == HB_MUSE_DISPATCH_QUEUED)
        a->phase = HB_MUSE_ADAPTER_BEGIN_WAIT;
      else
        failed(a);
      return;
    case HB_MUSE_ADAPTER_STREAM:
      if (a->queued) {
        size_t frames = HB_MUSE_ADAPTER_CAPACITY - a->head;
        if (frames > a->queued)
          frames = a->queued;
        if (frames > HB_MUSE_PCM_MAX_FRAMES)
          frames = HB_MUSE_PCM_MAX_FRAMES;
        hb_muse_audio_write_t wrote =
            a->transport.audio(a->user, a->id, a->consumed_frames, a->pcm + a->head, frames);
        if (wrote.status == HB_MUSE_AUDIO_BLOCKED && wrote.frames == 0)
          return;
        if (wrote.status != HB_MUSE_AUDIO_PROGRESS || !wrote.frames || wrote.frames > frames) {
          failed(a); /* Unknown acceptance: zero queue, never retry. */
          return;
        }
        memset(a->pcm + a->head, 0, wrote.frames * sizeof(*a->pcm));
        a->head = (a->head + wrote.frames) % HB_MUSE_ADAPTER_CAPACITY;
        a->queued -= wrote.frames;
        a->consumed_frames += (uint32_t)wrote.frames;
        return;
      }
      if (!a->end_requested)
        return;
      dispatch = a->transport.end(a->user, a->id, a->accepted_frames);
      if (dispatch == HB_MUSE_DISPATCH_QUEUED)
        a->phase = HB_MUSE_ADAPTER_END_WAIT;
      else if (dispatch != HB_MUSE_DISPATCH_BLOCKED)
        failed(a);
      return;
    case HB_MUSE_ADAPTER_CANCEL_PENDING:
      dispatch = a->transport.cancel(a->user, a->id);
      if (dispatch == HB_MUSE_DISPATCH_QUEUED)
        a->phase = HB_MUSE_ADAPTER_CANCEL_WAIT;
      else if (dispatch != HB_MUSE_DISPATCH_BLOCKED)
        a->phase = HB_MUSE_ADAPTER_FENCE_REQUIRED;
      return;
    default:
      return;
  }
}

bool hb_muse_pcm_adapter_event(hb_muse_pcm_adapter_t *a,
                               hb_muse_turn_id_t id,
                               hb_muse_adapter_event_t event) {
  if (!usable(a) || a->phase == HB_MUSE_ADAPTER_IDLE || !equal_id(id, a->id))
    return false;
  switch (event) {
    case HB_MUSE_ADAPTER_BEGIN_ACK:
      if (a->phase != HB_MUSE_ADAPTER_BEGIN_WAIT)
        return false;
      a->phase = HB_MUSE_ADAPTER_STREAM;
      return true;
    case HB_MUSE_ADAPTER_END_ACK:
      if (a->phase != HB_MUSE_ADAPTER_END_WAIT)
        return false;
      a->phase = HB_MUSE_ADAPTER_REPLY_WAIT;
      return true;
    case HB_MUSE_ADAPTER_TURN_DONE:
      if (a->phase != HB_MUSE_ADAPTER_REPLY_WAIT)
        return false;
      a->event_id = a->id;
      a->pending_event = HB_MUSE_EVENT_TURN_DONE;
      a->event_pending = true;
      retire(a);
      return true;
    case HB_MUSE_ADAPTER_TURN_FAILED:
      if (cancelling(a))
        return false;
      failed(a);
      return true;
    case HB_MUSE_ADAPTER_FENCED:
      if (!cancelling(a))
        return false;
      retire(a);
      return true;
    default:
      return false;
  }
}

bool hb_muse_pcm_adapter_take_event(hb_muse_pcm_adapter_t *a,
                                    hb_muse_turn_id_t *id,
                                    hb_muse_event_t *event) {
  if (!usable(a) || !a->event_pending || !id || !event)
    return false;
  *id = a->event_id;
  *event = a->pending_event;
  a->event_pending = false;
  return true;
}
