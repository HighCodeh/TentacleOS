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

/* Real SDK/PCM bridge, driven by one serialized owner. Not a device driver.
 * Init does NOT start the SDK, pair, read tokens, enable radio or microphone.
 * Before each pump process physical release/cancel/link loss on the core.
 */
#ifndef HB_MUSE_SDK_BINDING_H
#define HB_MUSE_SDK_BINDING_H
#include "hb_muse_pcm_adapter.h"
#include "hb_muse_sdk_port.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  hb_muse_pcm_adapter_t adapter;
  hb_muse_sdk_snapshot_t snapshot;
  uint64_t now_ms, phase_ms, progress_ms, turn_ms, operation_ms;
  uint32_t progress_frames;
  unsigned observed_operation;
  hb_muse_turn_id_t observed_id;
  hb_muse_adapter_phase_t observed_phase;
  bool initialized, owns_sdk, clock_started, clock_fault, audio_waiting, timed_out, snapshot_valid;
} hb_muse_sdk_binding_t;

bool hb_muse_sdk_binding_init(hb_muse_sdk_binding_t *binding);
/* At most one SDK dispatch/PCM write per pump; snapshots/acks are bounded.
 * Monotonic milliseconds required. No automatic microphone or new turn.
 * Completion/error goes through adapter_take_event -> core backend_event.
 * begin/end/cancel deadline 20s, audio backpressure 1s, whole turn 180s.
 * A cancellation deadline never fabricates a fence: session stays blocked.
 */
void hb_muse_sdk_binding_pump(hb_muse_sdk_binding_t *binding, uint64_t now_ms);
bool hb_muse_sdk_binding_connected(const hb_muse_sdk_binding_t *binding);
/* Future microphone owner must wait for this actual worker begin ACK, and
 * also check core capture permission/physical PTT, before capturing samples. */
bool hb_muse_sdk_binding_capture_ready(const hb_muse_sdk_binding_t *binding);
#ifdef __cplusplus
}
#endif
#endif
