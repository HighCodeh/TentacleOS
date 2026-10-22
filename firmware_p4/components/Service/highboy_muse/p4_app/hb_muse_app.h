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

/* Isolated P4 application controller. Not registered in TentacleOS yet.
 * Single serialized owner, non-ISR; callbacks must not re-enter. Handle actual
 * physical release/BACK before received frames, PCM or pump. No credentials,
 * simulated readiness, generated replies, speaker playback or auto reconnect.
 */
#ifndef HB_MUSE_APP_H
#define HB_MUSE_APP_H
#include "../core/hb_muse_core.h"
#include "../transport/hb_muse_wire_client.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HB_MUSE_APP_TEXT_CAP   1024u
#define HB_MUSE_APP_STATUS_MS  100u
#define HB_MUSE_APP_MAX_FRAMES 240000u
typedef enum { HB_MUSE_APP_ENABLE, HB_MUSE_APP_CONNECT, HB_MUSE_APP_REVOKE } hb_muse_app_control_t;
typedef struct {
  /* Trusted LOCAL RPC adapter, not a remote network route. True means only
   * local control acceptance. CONNECT is never retried automatically; its
   * driver must first observe C5 runtime PREPARED after ENABLE. Ambiguous
   * acceptance returns false, then the app requests REVOKE once. */
  bool (*control)(void *user, hb_muse_app_control_t operation);
  /* Copy the entire encoded frame into a bounded TX path. False consumes no
   * bytes; the wire client still counts this attempt as lost. No re-entry. */
  bool (*send)(void *user, const uint8_t *bytes, size_t size);
  /* Real hardware hooks. Start only after correlated SDK LISTENING/begin ACK.
   * stop must revoke recording synchronously, including partially failed start. */
  bool (*mic_start)(void *user, uint32_t rate_hz);
  void (*mic_stop)(void *user);
} hb_muse_app_ops_t;
typedef enum {
  HB_MUSE_APP_DISABLED,
  HB_MUSE_APP_OFFLINE,
  HB_MUSE_APP_CONNECTING,
  HB_MUSE_APP_READY,
  HB_MUSE_APP_STARTING,
  HB_MUSE_APP_LISTENING,
  HB_MUSE_APP_SENDING,
  HB_MUSE_APP_WAITING,
  HB_MUSE_APP_REPLY,
  HB_MUSE_APP_ERROR
} hb_muse_app_phase_t;
typedef enum {
  HB_MUSE_APP_NO_ERROR,
  HB_MUSE_APP_CONTROL_FAILED,
  HB_MUSE_APP_LINK_FAILED,
  HB_MUSE_APP_BAD_RESPONSE,
  HB_MUSE_APP_MIC_FAILED,
  HB_MUSE_APP_PEER_FAILED
} hb_muse_app_error_t;
typedef struct {
  hb_muse_app_phase_t phase;
  hb_muse_app_error_t error;
  bool enabled, sdk_ready, mic_active, text_complete, text_truncated;
  uint32_t accepted_frames, acknowledged_frames, text_revision, message_index;
  uint16_t text_bytes;
  const char *text; /* owned by app; valid only until next owner operation */
} hb_muse_app_view_t;
typedef struct {
  hb_muse_core_t core;
  hb_muse_wire_client_t wire;
  hb_muse_app_ops_t ops;
  void *user;
  hb_muse_turn_id_t turn;
  uint64_t now_ms, last_status_ms;
  uint32_t pcm_first, acknowledged, end_total, text_revision, message_index;
  uint16_t pcm_count, pcm_offset, text_total, text_offset;
  int16_t pcm[HB_MUSE_PCM_MAX_FRAMES];
  char text[HB_MUSE_APP_TEXT_CAP];
  hb_muse_app_phase_t phase;
  hb_muse_app_error_t error;
  uint8_t remote_phase;
  bool initialized, enabled, service_requested, connecting, sdk_ready, mic_active;
  bool armed, turn_active, begin_needed, end_needed, ended, text_complete;
  bool terminal, text_truncated, status_due, clock_started;
} hb_muse_app_t;

bool hb_muse_app_init(hb_muse_app_t *app, const hb_muse_app_ops_t *ops, void *user);
/* Physical confirmation only. Deliberately disarms PTT: confirmation cannot
 * record; a real release then fresh press is mandatory even if already ready. */
bool hb_muse_app_confirm(hb_muse_app_t *app, uint64_t now_ms);
/* Separate explicit physical CONNECT after C5 PREPARED. Nonce must be unique
 * across all discovery attempts AND P4 boots, supplied by reviewed platform. */
bool hb_muse_app_connect(hb_muse_app_t *app, uint64_t nonce, uint64_t now_ms);
hb_muse_result_t hb_muse_app_physical_ptt(hb_muse_app_t *app, bool pressed, uint64_t now_ms);
void hb_muse_app_close(hb_muse_app_t *app);  /* BACK/consent revocation */
void hb_muse_app_cancel(hb_muse_app_t *app); /* no automatic reconnect */
/* Actual mic-driver samples only. Call capacity BEFORE reading I2S. One chunk
 * <=320 copied atomically; wire splits it into <=106-sample acknowledged pages.
 * Accepted pre-release audio drains before END; cancel destroys it immediately.
 * The 240000-frame limit stops I2S and sends the bounded turn; rearming still
 * requires a physical release. It never creates a new press automatically.
 * A driver overrun/discontinuity must cancel; never silently skip PCM samples. */
size_t hb_muse_app_pcm_capacity(const hb_muse_app_t *app);
hb_muse_result_t hb_muse_app_pcm(hb_muse_app_t *app, const int16_t *pcm, size_t frames);
/* Bounded: at most one outbound wire attempt per pump; no sleeps/retry loop.
 * Poll replies regularly and keep pumping even while not recording. */
void hb_muse_app_pump(hb_muse_app_t *app, uint64_t now_ms);
void hb_muse_app_receive(hb_muse_app_t *app, const uint8_t *frame, size_t size, uint64_t now_ms);
bool hb_muse_app_view(const hb_muse_app_t *app, hb_muse_app_view_t *out);
#ifdef __cplusplus
}
#endif
#endif
