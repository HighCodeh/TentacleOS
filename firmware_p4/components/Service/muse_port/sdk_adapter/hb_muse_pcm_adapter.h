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

/* Isolated High Boy/Muse PCM adapter. NOT connected to the SDK or hardware.
 *
 * One serialized owner must call every function, including event delivery and
 * pump. Process pending physical release/cancel/disconnect BEFORE PCM or pump.
 * Callbacks are nonblocking, do not retain PCM pointers and must not re-enter.
 * No mutex, ISR entry, network, microphone, credentials or fake SDK success.
 */
#ifndef HB_MUSE_PCM_ADAPTER_H
#define HB_MUSE_PCM_ADAPTER_H

#include "../core/hb_muse_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HB_MUSE_ADAPTER_CAPACITY (4u * HB_MUSE_PCM_MAX_FRAMES) /* 80 ms, 2560 B */

typedef enum {
  HB_MUSE_DISPATCH_QUEUED,  /* dispatch only; not start/end/cancel completion */
  HB_MUSE_DISPATCH_BLOCKED, /* definitely no command accepted */
  HB_MUSE_DISPATCH_FAILED   /* uncertain; never automatically retry */
} hb_muse_dispatch_t;

typedef enum {
  HB_MUSE_AUDIO_PROGRESS, /* precisely frames > 0 consumed */
  HB_MUSE_AUDIO_BLOCKED,  /* precisely zero frames consumed */
  HB_MUSE_AUDIO_FAILED    /* uncertain prefix: abandon, never replay */
} hb_muse_audio_status_t;

typedef struct {
  hb_muse_audio_status_t status;
  size_t frames;
} hb_muse_audio_write_t;

typedef struct {
  hb_muse_dispatch_t (*begin)(void *user, hb_muse_turn_id_t id);
  /* Copied/consumed prefix only. An actual audio_wait(pcm,n,0) return n>0
   * maps to PROGRESS(n), 0 maps to BLOCKED(0), AFTER a verified begin ACK.
   * This function must not silently consume a different/unknown prefix.
   */
  hb_muse_audio_write_t (*audio)(
      void *user, hb_muse_turn_id_t id, uint32_t first_frame, const int16_t *pcm, size_t frames);
  hb_muse_dispatch_t (*end)(void *user, hb_muse_turn_id_t id, uint32_t total_frames);
  hb_muse_dispatch_t (*cancel)(void *user, hb_muse_turn_id_t id);
} hb_muse_adapter_transport_t;

typedef enum {
  HB_MUSE_ADAPTER_IDLE,
  HB_MUSE_ADAPTER_BEGIN_PENDING,
  HB_MUSE_ADAPTER_BEGIN_WAIT,
  HB_MUSE_ADAPTER_STREAM,
  HB_MUSE_ADAPTER_END_WAIT,
  HB_MUSE_ADAPTER_REPLY_WAIT,
  HB_MUSE_ADAPTER_CANCEL_PENDING,
  HB_MUSE_ADAPTER_CANCEL_WAIT,
  HB_MUSE_ADAPTER_FENCE_REQUIRED
} hb_muse_adapter_phase_t;

typedef enum {
  /* Real, ID-correlated local SDK acknowledgments, NOT inferred from void
   * SDK calls or merely Wi-Fi/TLS/ready(). None imply an assistant reply. */
  HB_MUSE_ADAPTER_BEGIN_ACK,
  HB_MUSE_ADAPTER_END_ACK,
  /* Real terminal completion AND old SDK audio/event paths drained. Raw
   * upstream EV_DONE alone is insufficient: its audio may still be queued. */
  HB_MUSE_ADAPTER_TURN_DONE,
  HB_MUSE_ADAPTER_TURN_FAILED,
  /* SDK worker/old PCM/event paths have actually been stopped and drained.
   * A dispatched cancel alone is insufficient. Required before a new turn
   * after cancellation; verified connection teardown may fence even before
   * the pending cancel command can be dispatched. */
  HB_MUSE_ADAPTER_FENCED
} hb_muse_adapter_event_t;

/* Allocate in suitable caller-owned memory; fields private outside tests.
 * No heap allocations. The ring contains signed PCM16 mono at 16 kHz. */
typedef struct {
  hb_muse_adapter_transport_t transport;
  void *user;
  int16_t pcm[HB_MUSE_ADAPTER_CAPACITY];
  size_t head, queued;
  uint32_t accepted_frames, consumed_frames;
  hb_muse_turn_id_t id, last_id, event_id;
  hb_muse_event_t pending_event;
  hb_muse_adapter_phase_t phase;
  bool valid, end_requested, transport_touched, event_pending;
} hb_muse_pcm_adapter_t;

/* Initialize once on fresh storage, never over a live SDK turn. */
bool hb_muse_pcm_adapter_init(hb_muse_pcm_adapter_t *adapter,
                              const hb_muse_adapter_transport_t *transport,
                              void *user);
/* Supply this backend and adapter as user to hb_muse_init(). Core ACCEPTED
 * means the adapter owns a bounded local begin/audio/end operation; it NEVER
 * means the SDK, network or assistant has completed it. PCM is copied in full.
 * Backpressure leaves every sample and counter untouched.
 */
const hb_muse_backend_t *hb_muse_pcm_adapter_backend(void);

/* At most ONE transport callback per call. Never busy-waits or retries an
 * uncertain command/audio. A BLOCKED result may be retried on a later pump.
 * Queued PCM recorded BEFORE release may drain; no NEW PCM after end/cancel.
 * Integration must set bounded wall-clock deadlines for ACKs/backpressure and
 * report TURN_FAILED on expiry; this layer has no timer or scheduler.
 */
void hb_muse_pcm_adapter_pump(hb_muse_pcm_adapter_t *adapter);
/* Stale, premature or duplicate events are rejected without state changes.
 * TURN_FAILED also handles link loss/deadline expiry; local queue is erased
 * immediately. Deliver core link loss as well to revoke readiness/generation.
 */
bool hb_muse_pcm_adapter_event(hb_muse_pcm_adapter_t *adapter,
                               hb_muse_turn_id_t id,
                               hb_muse_adapter_event_t event);
/* Single terminal-event slot: blocks new turns until delivered to the core.
 * Caller passes returned ID/event to hb_muse_backend_event(). No manufactured
 * transcript, answer, audio or completion. Cancellation itself emits no DONE.
 */
bool hb_muse_pcm_adapter_take_event(hb_muse_pcm_adapter_t *adapter,
                                    hb_muse_turn_id_t *id,
                                    hb_muse_event_t *event);

#ifdef __cplusplus
}
#endif
#endif
