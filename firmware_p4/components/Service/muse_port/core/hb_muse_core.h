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

/* High Boy Muse integration foundation, not an SDK or hardware connection.
 *
 * This dependency-free controller does not pair, authenticate, connect, record,
 * play audio, call an API, or produce assistant replies. A future adapter must
 * provide real readiness and implement the backend callbacks. No token belongs
 * in this API. Existing High Boy firmware is not changed by these files.
 *
 * All calls belong to ONE serialized owner. This is not an ISR/thread-safe API.
 * Deliver pending release/disconnect/cancel input before offering captured PCM.
 * Callbacks must be short, nonblocking, and must not re-enter the controller.
 */
#ifndef HB_MUSE_CORE_H
#define HB_MUSE_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HB_MUSE_PCM_RATE_HZ    16000u
#define HB_MUSE_PCM_CHANNELS   1u
#define HB_MUSE_PCM_MAX_FRAMES 320u /* 20 ms of signed PCM16 mono. */

/* Local IDs only: these are not Muse account, request or authentication IDs. */
typedef struct {
  uint32_t generation;
  uint32_t turn;
} hb_muse_turn_id_t;

typedef enum {
  HB_MUSE_IO_ACCEPTED,
  /* No data or side effect was accepted; only audio may retry this result. */
  HB_MUSE_IO_WOULD_BLOCK,
  /* Acceptance is uncertain. The controller cancels; it never retries. */
  HB_MUSE_IO_FAILED
} hb_muse_io_result_t;

typedef struct {
  hb_muse_io_result_t (*begin)(void *user, hb_muse_turn_id_t id);
  /* Copy/consume the ENTIRE chunk before returning ACCEPTED. Retaining the
   * pointer, accepting a prefix, or silently dropping audio is prohibited.
   * WOULD_BLOCK consumes nothing: caller retains the exact chunk for retry.
   */
  hb_muse_io_result_t (*audio)(
      void *user, hb_muse_turn_id_t id, uint32_t first_frame, const int16_t *pcm, size_t frames);
  hb_muse_io_result_t (*end)(void *user, hb_muse_turn_id_t id, uint32_t total_frames);
  /* Local cancellation must take effect even if the transport is gone.
   * Delivery of remote cancellation is the adapter's responsibility.
   */
  void (*cancel)(void *user, hb_muse_turn_id_t id);
} hb_muse_backend_t;

typedef enum {
  HB_MUSE_DISABLED,
  HB_MUSE_DISCONNECTED,
  HB_MUSE_READY,
  HB_MUSE_CAPTURING,
  HB_MUSE_WAITING_REPLY,
  HB_MUSE_ERROR
} hb_muse_state_t;

typedef enum {
  HB_MUSE_OK,
  HB_MUSE_IGNORED,
  HB_MUSE_NOT_READY,
  HB_MUSE_INVALID_ARGUMENT,
  HB_MUSE_STALE,
  HB_MUSE_OUT_OF_ORDER,
  HB_MUSE_BACKPRESSURE,
  HB_MUSE_BACKEND_ERROR,
  HB_MUSE_ID_EXHAUSTED
} hb_muse_result_t;

typedef enum { HB_MUSE_EVENT_TURN_DONE, HB_MUSE_EVENT_TURN_FAILED } hb_muse_event_t;

/* Public for allocation without malloc. Treat fields as private outside tests. */
typedef struct {
  hb_muse_backend_t backend;
  void *user;
  hb_muse_turn_id_t active_id;
  uint32_t generation, next_turn, accepted_frames;
  hb_muse_state_t state;
  bool valid, enabled, paired, network_up, transport_ready, ptt_held;
  bool active, exhausted;
} hb_muse_core_t;

/* Initialize once on fresh storage: starts DISABLED and unpaired/offline.
 * All four callbacks are mandatory. Do not reinitialize a live instance.
 */
bool hb_muse_init(hb_muse_core_t *core, const hb_muse_backend_t *backend, void *user);
uint32_t hb_muse_generation(const hb_muse_core_t *core);
hb_muse_state_t hb_muse_state(const hb_muse_core_t *core);
bool hb_muse_active_turn(const hb_muse_core_t *core, hb_muse_turn_id_t *out);

/* Local user opt-in. Enabling never begins a turn or clears a held button.
 * Disabling revokes capture, cancels an active turn and changes generation.
 */
hb_muse_result_t hb_muse_set_enabled(hb_muse_core_t *core, bool enabled);

/* Only the real adapter may report these facts. A stale generation is ignored.
 * transport_ready means paired SDK session ready, not merely Wi-Fi/TLS up.
 * Contradictory readiness is rejected and invalidates the connection. Losing
 * any previously true prerequisite cancels and changes generation;
 * a subsequent ready event must carry the NEW generation. No auto-reconnect.
 */
hb_muse_result_t hb_muse_link_update(
    hb_muse_core_t *core, uint32_t generation, bool paired, bool network_up, bool transport_ready);
/* Explicitly invalidate callbacks/frames from a closed or replaced connection. */
hb_muse_result_t hb_muse_connection_reset(hb_muse_core_t *core);

/* Edges from the physical input adapter, not a simulated timer or remote event.
 * Only a fresh rising edge while enabled and ready may begin. A release revokes
 * capture BEFORE end/cancel is called. A press while waiting is not barge-in:
 * explicitly cancel, then release and press again.
 */
hb_muse_result_t hb_muse_ptt(hb_muse_core_t *core, bool held);
bool hb_muse_capture_allowed(const hb_muse_core_t *core, hb_muse_turn_id_t id);

/* No internal audio queue. first_frame starts at 0 and advances only after a
 * complete ACCEPTED chunk. The caller may retry WOULD_BLOCK with the same
 * chunk only while capture_allowed remains true. Invalid/stale/out-of-order
 * chunks are rejected without delivery. Ambiguous backend failure cancels.
 */
hb_muse_result_t hb_muse_offer_pcm(hb_muse_core_t *core,
                                   hb_muse_turn_id_t id,
                                   uint32_t first_frame,
                                   const int16_t *pcm,
                                   size_t frames);
hb_muse_result_t hb_muse_cancel(hb_muse_core_t *core);

/* Events for the exact active turn. DONE is valid only after release/end
 * acceptance; FAILED also revokes a turn that is still capturing. These events
 * cannot change connectivity or enable recording. No text/TTS is fabricated.
 */
hb_muse_result_t
hb_muse_backend_event(hb_muse_core_t *core, hb_muse_turn_id_t id, hb_muse_event_t event);

#ifdef __cplusplus
}
#endif
#endif
