/* Portable, single-owner P4-side transport only. No SPI, SDK, microphone or
 * API calls. CRC is not authentication. See hb_muse_wire.h for the peer.
 */
#ifndef HB_MUSE_WIRE_CLIENT_H
#define HB_MUSE_WIRE_CLIENT_H
#include "hb_muse_wire.h"
#ifdef __cplusplus
extern "C" {
#endif

#define HB_MUSE_CLIENT_RETRY_MS   250u
#define HB_MUSE_CLIENT_ATTEMPTS   3u
#define HB_MUSE_CLIENT_REQUEST_MS 750u

typedef enum {
  HB_MUSE_CLIENT_NEED_HELLO,
  HB_MUSE_CLIENT_CAN_OPEN,
  HB_MUSE_CLIENT_PEER_BUSY,
  HB_MUSE_CLIENT_ACTIVE
} hb_muse_client_state_t;
typedef enum {
  HB_MUSE_CLIENT_FAULT_NONE,
  HB_MUSE_CLIENT_FAULT_ABANDONED,
  HB_MUSE_CLIENT_FAULT_TIMEOUT,
  HB_MUSE_CLIENT_FAULT_LEASE,
  HB_MUSE_CLIENT_FAULT_CLOCK,
  HB_MUSE_CLIENT_FAULT_PROTOCOL,
  HB_MUSE_CLIENT_FAULT_REBOOT,
  HB_MUSE_CLIENT_FAULT_EXHAUSTED
} hb_muse_client_fault_t;
typedef enum {
  HB_MUSE_CLIENT_IGNORED,
  HB_MUSE_CLIENT_RESPONSE,
  HB_MUSE_CLIENT_LINK_LOST
} hb_muse_client_accept_t;

/* Public for static allocation only. Treat fields as private outside tests.
 * The configured lease must match the server and exceed REQUEST_MS. Init does
 * not send anything. A clock rollback permanently faults this instance until
 * init; all other faults require explicit HELLO then OPEN (never auto-BEGIN).
 */
typedef struct {
  hb_muse_client_state_t state;
  hb_muse_client_fault_t fault;
  hb_muse_wire_frame_t request;
  uint8_t encoded[HB_MUSE_WIRE_FRAME_MAX];
  size_t encoded_len;
  uint32_t boot, max_epoch, epoch, last_sequence, lease_ms;
  uint64_t clock_ms, first_send_ms, last_send_ms, lease_start_ms, discovery_nonce;
  unsigned attempts;
  bool initialized, clock_started, clock_fault, pending, sent;
} hb_muse_wire_client_t;

bool hb_muse_wire_client_init(hb_muse_wire_client_t *client, uint32_t lease_ms);
void hb_muse_wire_client_tick(hb_muse_wire_client_t *client, uint64_t now_ms);
/* Caller must supply a DIFFERENT nonzero 64-bit nonce for EVERY discovery,
 * including across client init/reboot. Platform must provide non-reuse; merely
 * truncated/random IDs can collide. We additionally reject immediate reuse
 * across abandon/recovery. Nonce freshness prevents delayed handshake replies,
 * not malicious forgery; CRC still provides no peer authentication.
 */
bool hb_muse_wire_client_prepare_hello(hb_muse_wire_client_t *client,
                                       uint64_t discovery_nonce,
                                       uint64_t now_ms);
/* Uses the most recent inactive HELLO's max_epoch + 1. Never steals an active
 * peer lease. A BUSY result requires another explicit HELLO before OPEN.
 */
bool hb_muse_wire_client_prepare_open(hb_muse_wire_client_t *client, uint64_t now_ms);
bool hb_muse_wire_client_prepare_command(hb_muse_wire_client_t *client,
                                         uint8_t opcode,
                                         uint32_t generation,
                                         uint32_t turn,
                                         const uint8_t *payload,
                                         size_t payload_len,
                                         uint64_t now_ms);
/* One bounded transmission attempt, not a blocking retry loop. Owner must send
 * these bytes immediately or count the attempt as lost. First attempt emits
 * immediately; at most two exact-byte retries follow at >=250 ms intervals.
 * At 750 ms without a validated reply the lease is abandoned. Capacity must
 * fit the encoded frame; no attempt is consumed on insufficient capacity.
 * Does not return request bytes unless true; *length is zero otherwise.
 * A physical release/cancel must be handled BEFORE calling this function.
 */
bool hb_muse_wire_client_next_request(hb_muse_wire_client_t *client,
                                      uint64_t now_ms,
                                      uint8_t *bytes,
                                      size_t capacity,
                                      size_t *length);
/* Only RESPONSE writes *response. Malformed, unsolicited and foreign frames
 * expose no payload and do not refresh the lease or consume a sequence.
 * Exact correlated GAP/CONFLICT/STALE loses the link. A changed boot plus
 * STALE also loses it. Application REJECTED is reported but conservatively
 * abandons the lease because peer callback acceptance may be uncertain.
 * BLOCKED/BUSY applications consume a sequence; retry with a new command.
 */
hb_muse_client_accept_t hb_muse_wire_client_accept_response(hb_muse_wire_client_t *client,
                                                            const uint8_t *bytes,
                                                            size_t length,
                                                            uint64_t now_ms,
                                                            hb_muse_wire_frame_t *response);
/* Immediate local revocation, not a remote cancellation acknowledgement.
 * Pending PCM/END bytes are destroyed and never retried. Do not capture/send
 * more audio. The server must expire its lease and VERIFY SDK fencing before
 * accepting the next OPEN. Owner can send CANCEL as an ordinary command only
 * when no request is outstanding; uncertain in-flight work requires abandon.
 */
void hb_muse_wire_client_abandon(hb_muse_wire_client_t *client);

#ifdef __cplusplus
}
#endif
#endif
