/* Local P4/C5 service dispatch. Not a network or host-link API.
 * One serialized SPI task; all callbacks bounded/nonblocking except lazy
 * task creation. Cloud connection/readiness remain separate facts. */
#ifndef HB_MUSE_RPC_H
#define HB_MUSE_RPC_H
#include "../runtime/hb_muse_runtime.h"
#define HB_MUSE_RPC_VERSION      1u
#define HB_MUSE_RPC_STATUS_BYTES 32u
enum {
  HB_MUSE_RPC_ENABLE = 0x1101,
  HB_MUSE_RPC_CONNECT = 0x1102,
  HB_MUSE_RPC_REVOKE = 0x1103,
  HB_MUSE_RPC_STATUS = 0x1104,
  HB_MUSE_RPC_TX = 0x1105,
  HB_MUSE_RPC_PULL = 0x1106,
  HB_MUSE_RPC_PULL_REPLAY = 0x110c
};
/* Numeric values intentionally match the existing SPI status values. */
typedef enum {
  HB_MUSE_RPC_OK = 0,
  HB_MUSE_RPC_BUSY = 1,
  HB_MUSE_RPC_ERROR = 2,
  HB_MUSE_RPC_UNSUPPORTED = 3,
  HB_MUSE_RPC_INVALID = 4
} hb_muse_rpc_result_t;
typedef struct {
  bool (*ensure_initialized)(void *);
  bool (*enable)(void *, bool);
  bool (*connect)(void *);
  void (*revoke)(void *);
  bool (*status)(void *, hb_muse_runtime_status_t *);
  bool (*submit)(void *, const uint8_t *, size_t);
  bool (*take_reply)(void *, uint8_t *, size_t, size_t *);
} hb_muse_rpc_ops_t;
typedef struct {
  hb_muse_rpc_ops_t ops;
  void *user;
  bool initialized;
  uint32_t pull_sequence, pull_revision;
  size_t pull_size;
  uint8_t pull_cache[HB_MUSE_RUNTIME_FRAME_MAX];
} hb_muse_rpc_t;
/* PULL_REPLAY: [version, LE32 sequence], beginning at 1 after ENABLE.
 * Response [LE32 sequence, zero or one unchanged wire frame], capacity >=252.
 * Same sequence replays the cached result (including empty); next sequence
 * acknowledges it and consumes at most once. Gaps/old/zero sequences fail.
 * Cache is volatile and invalid across revoke, enable, or session revision.
 * Only this operation permits same-sequence transport retries. */
/* Registration only; no callbacks or task/hardware/network initialization. */
bool hb_muse_rpc_init(hb_muse_rpc_t *, const hb_muse_rpc_ops_t *, void *);
/* ENABLE payload [1, bool]; CONNECT/REVOKE/STATUS/PULL payload [1].
 * TX payload is one exact v1 Muse wire frame. Response capacity >=248 before
 * ANY mutation. OK for control/TX means local acceptance, never ready/paired.
 * CONNECT cannot be automatically retried after ambiguous SPI delivery.
 * PULL may be empty; lost wire replies use exact endpoint-sequence replay.
 * STATUS is nonconsuming, [version, phase, error, flags] then LE counters:
 * 4 requested_revision,8 applied_revision,12 accepted,16 rejected,20 processed,
 * 24 session_limit_ms,28 frame_max:u16,30 queue_slots,31 reserved.
 * Flags: enabled1,paired2,network4,authorized8,worker16,ready32,fenced64,init128.
 * Missing runtime returns init=0/ready=0; never lazily starts on STATUS/PULL.
 */
hb_muse_rpc_result_t hb_muse_rpc_execute(hb_muse_rpc_t *,
                                         uint16_t command,
                                         const uint8_t *input,
                                         size_t input_size,
                                         uint8_t *output,
                                         size_t capacity,
                                         size_t *output_size);
#endif
