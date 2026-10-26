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

/* Isolated High Boy/Muse wire protocol, not firmware or a device driver.
 * Single serialized owner; callbacks must not re-enter. CRC detects accidental
 * corruption only: this protocol does NOT authenticate or encrypt a peer.
 * No SDK startup, network, token, microphone or hardware operation is provided.
 */
#ifndef HB_MUSE_WIRE_H
#define HB_MUSE_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define HB_MUSE_WIRE_MAGIC            0x4d48u
#define HB_MUSE_WIRE_VERSION          1u
#define HB_MUSE_WIRE_HEADER_BYTES     28u
#define HB_MUSE_WIRE_PAYLOAD_MAX      216u
#define HB_MUSE_WIRE_CRC_BYTES        4u
#define HB_MUSE_WIRE_FRAME_MAX        248u
#define HB_MUSE_WIRE_RESPONSE         1u
#define HB_MUSE_WIRE_DEFAULT_LEASE_MS 2000u
#define HB_MUSE_WIRE_NONCE_BYTES      8u
#define HB_MUSE_WIRE_HELLO_BYTES      13u /* max_epoch LE32, active byte (0/1), nonce[8] */
#define HB_MUSE_WIRE_OPEN_BYTES       8u  /* echoed discovery nonce */

typedef enum {
  HB_MUSE_WIRE_HELLO = 1,
  HB_MUSE_WIRE_OPEN = 2,
  HB_MUSE_WIRE_BEGIN = 3,
  HB_MUSE_WIRE_PCM = 4,
  HB_MUSE_WIRE_END = 5,
  HB_MUSE_WIRE_CANCEL = 6,
  HB_MUSE_WIRE_STATUS = 7,
  HB_MUSE_WIRE_TEXT = 8,
  HB_MUSE_WIRE_RETIRE = 9,
  HB_MUSE_WIRE_CLOSE = 10
} hb_muse_wire_opcode_t;
typedef enum {
  HB_MUSE_WIRE_OK = 0,
  HB_MUSE_WIRE_BLOCKED = 1,
  HB_MUSE_WIRE_REJECTED = 2,
  HB_MUSE_WIRE_CONFLICT = 3,
  HB_MUSE_WIRE_GAP = 4,
  HB_MUSE_WIRE_STALE = 5,
  HB_MUSE_WIRE_BUSY = 6
} hb_muse_wire_status_t;

/* In-memory representation is NOT the serialized layout. Integer fields are
 * explicitly LE on the wire, followed by CRC-32/ISO-HDLC (reflected IEEE).
 * Offset: magic0/version2/op3/boot4/epoch8/seq12/gen16/turn20/len24/status26/flags27.
 */
typedef struct {
  uint32_t boot, epoch, sequence, generation, turn;
  uint16_t payload_len;
  uint8_t opcode, status, flags;
  uint8_t payload[HB_MUSE_WIRE_PAYLOAD_MAX];
} hb_muse_wire_frame_t;

uint32_t hb_muse_wire_crc32(const uint8_t *bytes, size_t length);
bool hb_muse_wire_encode(const hb_muse_wire_frame_t *frame,
                         uint8_t *bytes,
                         size_t capacity,
                         size_t *length);
/* Exact length required: padding, truncation, extra flags, unknown op/version,
 * request status !=0 and invalid CRC fail without changing *frame. */
bool hb_muse_wire_decode(const uint8_t *bytes, size_t length, hb_muse_wire_frame_t *frame);

typedef struct {
  /* True only when the local application verifies old work is fenced and
   * idle. Used for OPEN/CLOSE; it must not start the SDK or perform pairing. */
  bool (*can_open)(void *user);
  /* Called once for an accepted next-sequence application command. The
   * callback may write <=216 reply bytes and must set *reply_len. BLOCKED
   * guarantees no request data consumed. Returned statuses are cached and
   * consume the sequence; retry work with a NEW sequence, never replay it.
   * Only OK/BLOCKED/REJECTED/BUSY are valid callback results. */
  hb_muse_wire_status_t (*dispatch)(void *user,
                                    const hb_muse_wire_frame_t *request,
                                    uint8_t *reply,
                                    uint16_t *reply_len);
  /* Revoke capture/local work on lease expiry or backward clock. Nonblocking;
   * request actual asynchronous fencing. New OPEN remains gated by can_open. */
  void (*revoke)(void *user);
  /* Optional successful-new-epoch notification, AFTER can_open and commit.
   * Local metadata reset only; cannot fail/re-enter/start SDK or perform I/O.
   * Exact OPEN duplicates never call it. */
  void (*opened)(void *user, uint32_t epoch);
} hb_muse_wire_callbacks_t;

/* Public for allocation only. Treat fields as private outside tests. Cache is
 * exact serialized bytes; reboot must init with a DIFFERENT nonzero boot ID.
 * Application provides boot uniqueness: random/truncated IDs can collide.
 * HELLO and OPEN carry the SAME nonzero eight-byte discovery nonce supplied
 * by the P4 platform, unique across discovery attempts and P4 resets. HELLO
 * echoes it after the public max_epoch/active fields. OPEN responses echo it
 * when the request contains a valid nonce, including refusal statuses. This
 * rejects delayed replies from older discoveries; it is NOT authentication.
 */
typedef struct {
  hb_muse_wire_callbacks_t callbacks;
  void *user;
  uint32_t boot, max_epoch, active_epoch, last_sequence, lease_ms;
  uint64_t last_clock_ms, last_activity_ms;
  size_t cached_request_len, cached_response_len;
  uint8_t cached_request[HB_MUSE_WIRE_FRAME_MAX];
  uint8_t cached_response[HB_MUSE_WIRE_FRAME_MAX];
  bool initialized, active, clock_started, clock_fault, cache_valid, cache_is_close;
} hb_muse_wire_server_t;

/* Fresh storage only; never reinitialize a live epoch in place. */
bool hb_muse_wire_server_init(hb_muse_wire_server_t *server,
                              uint32_t boot,
                              uint32_t lease_ms,
                              const hb_muse_wire_callbacks_t *callbacks,
                              void *user);
/* Must be called regularly even without incoming SPI messages. lease_ms=0 at
 * init selects 2000ms. Monotonic uint64 milliseconds; backward time revokes
 * once and permanently faults this instance until explicit reinitialization.
 */
void hb_muse_wire_server_expire(hb_muse_wire_server_t *server, uint64_t now_ms);
/* response capacity must be >=248 BEFORE any callback/sequence mutation.
 * Malformed envelope/CRC produces no response and returns false. Protocol
 * rejections return a valid response. Exact last-request duplicates return
 * cached bytes with no callback or lease refresh. HELLO is entirely read-only;
 * caller must regularly call server_expire even while only HELLOs arrive.
 * Time expiry is evaluated independently of request acceptance; old epochs,
 * gaps/conflicts do not advance sequence, epoch or lease. No sequence wrap.
 * CLOSE only succeeds when can_open confirms fencing, retains the epoch
 * tombstone and is itself replayable after closure if its response was lost.
 * A next-sequence CLOSE with invalid IDs/payload is REJECTED and consumed/
 * cached without closing, consistently with application-command rejection.
 */
bool hb_muse_wire_server_process(hb_muse_wire_server_t *server,
                                 const uint8_t *request,
                                 size_t request_len,
                                 uint64_t now_ms,
                                 uint8_t *response,
                                 size_t response_capacity,
                                 size_t *response_len);

#ifdef __cplusplus
}
#endif
#endif
