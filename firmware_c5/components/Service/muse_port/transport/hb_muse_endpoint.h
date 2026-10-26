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

/* Isolated C5 endpoint. One serialized owner, never ISR/reentrant.
 * Owns no Wi-Fi/BLE, pairing, SDK startup, token, microphone or SPI driver.
 * Do not expose directly to a network: framing/CRC is not authentication.
 */
#ifndef HB_MUSE_ENDPOINT_H
#define HB_MUSE_ENDPOINT_H
#include "hb_muse_wire.h"
#include "../sdk_adapter/hb_muse_sdk_binding.h"
#ifdef __cplusplus
extern "C" {
#endif

#define HB_MUSE_ENDPOINT_PCM_FRAMES   106u /* first_frame LE32 + PCM16 LE */
#define HB_MUSE_ENDPOINT_STATUS_BYTES 32u
#define HB_MUSE_ENDPOINT_TEXT_HEADER  12u
typedef enum {
  HB_MUSE_ENDPOINT_IDLE,
  HB_MUSE_ENDPOINT_STARTING,
  HB_MUSE_ENDPOINT_LISTENING,
  HB_MUSE_ENDPOINT_FINISHING,
  HB_MUSE_ENDPOINT_WAITING,
  HB_MUSE_ENDPOINT_CANCELLING,
  HB_MUSE_ENDPOINT_DONE,
  HB_MUSE_ENDPOINT_FAILED,
  HB_MUSE_ENDPOINT_CANCELLED
} hb_muse_endpoint_phase_t;

/* STATUS is a cumulative, non-destructive snapshot:
 * 0 phase, 1 flags(enabled1/paired2/network4/sdk-ready8/begin16/end32/note64/fenced128),
 * 2 reserved0, 3 text-truncated, 4 accepted:u32, 8 consumed:u32, 12 text-revision:u32,
 * 16 message-index:u32, 20 text-bytes:u16, 22 ring-free:u16, 24 max-turn-frames:u32,
 * 28 max-wire-PCM-frames:u16, 30 sample-rate:u16. All integers LE.
 * Before BEGIN, query with generation=turn=0; during/after a turn use its IDs.
 * BEGIN: zero payload, fresh nonzero generation/turn. Acceptance is local only.
 * PCM: first-frame:u32 then 1..106 signed PCM16 samples; OK owns whole chunk,
 * BLOCKED owns none. END: exact accepted total:u32, nonzero. CANCEL: empty.
 * TEXT request: revision:u32, offset:u16. Reply: revision:u32, message:u32,
 * total:u16, offset:u16, then <=204 bytes (byte pages may split UTF-8).
 * A revision mismatch returns BLOCKED, never a mixture of old/new pages.
 * RETIRE: empty, only after terminal state AND verified SDK/local fencing.
 * New transport epoch may discard the previous fenced text/terminal state.
 */
typedef struct {
  hb_muse_wire_server_t server;
  hb_muse_sdk_binding_t binding;
  hb_muse_turn_id_t external_id, last_external_id, sdk_id;
  uint32_t epoch, local_turn, accepted, consumed, text_revision, message_index;
  uint16_t text_bytes;
  char text[HB_MUSE_SDK_TEXT_CAP];
  hb_muse_endpoint_phase_t phase;
  bool initialized, enabled, paired, network, active, cancelling;
  bool begin_ack, end_ack, note_ack, text_truncated, terminal_failed;
} hb_muse_endpoint_t;

/* boot must be a new nonzero ID on every C5 boot; uniqueness is a separate
 * platform obligation. Never reinit over a live SDK or active endpoint.
 */
bool hb_muse_endpoint_init(hb_muse_endpoint_t *endpoint, uint32_t boot);
/* Facts from actual local consent/account/network owners, NOT wire/UI guesses.
 * Revocation clears queued PCM immediately and requests asynchronous SDK fence.
 * These calls do not start the SDK or perform account/network requests.
 */
void hb_muse_endpoint_access(hb_muse_endpoint_t *endpoint, bool enabled, bool paired, bool network);
/* Call frequently EVEN without SPI input. Expire transport before SDK work.
 * The same owner must handle input release/cancel BEFORE PCM/process/pump.
 */
void hb_muse_endpoint_pump(hb_muse_endpoint_t *endpoint, uint64_t now_ms);
/* Dispatch only bounded local work; caller must pump regularly. Never treats
 * SPI/HELLO/OPEN success as an SDK or assistant reply. No automatic turns.
 */
bool hb_muse_endpoint_process(hb_muse_endpoint_t *endpoint,
                              const uint8_t *request,
                              size_t request_len,
                              uint64_t now_ms,
                              uint8_t *response,
                              size_t capacity,
                              size_t *response_len);
#ifdef __cplusplus
}
#endif
#endif
