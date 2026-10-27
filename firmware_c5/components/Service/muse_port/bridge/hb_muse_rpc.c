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

#include "hb_muse_rpc.h"
#include "../transport/hb_muse_wire.h"
#include <string.h>
static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}
static void clear_pull(hb_muse_rpc_t *rpc) {
  rpc->pull_sequence = 0;
  rpc->pull_size = 0;
  rpc->pull_revision = 0;
  memset(rpc->pull_cache, 0, sizeof rpc->pull_cache);
}
bool hb_muse_rpc_init(hb_muse_rpc_t *rpc, const hb_muse_rpc_ops_t *ops, void *user) {
  if (!rpc || !ops || !ops->ensure_initialized || !ops->enable || !ops->connect || !ops->revoke ||
      !ops->status || !ops->submit || !ops->take_reply)
    return false;
  *rpc = (hb_muse_rpc_t){.ops = *ops, .user = user, .initialized = true};
  return true;
}
static void status_bytes(hb_muse_rpc_t *rpc, uint8_t *out) {
  hb_muse_runtime_status_t s = {0};
  bool valid = rpc->ops.status(rpc->user, &s);
  memset(out, 0, HB_MUSE_RPC_STATUS_BYTES);
  out[0] = HB_MUSE_RPC_VERSION;
  if (valid) {
    out[1] = (uint8_t)s.phase;
    out[2] = (uint8_t)s.error;
    out[3] = (uint8_t)(128u | (s.enabled ? 1u : 0u) | (s.paired ? 2u : 0u) | (s.network ? 4u : 0u) |
                       (s.cloud_authorized ? 8u : 0u) | (s.worker_started ? 16u : 0u) |
                       (s.session_ready ? 32u : 0u) | (s.fenced ? 64u : 0u));
    put32(out + 4, s.session_requested_revision);
    put32(out + 8, s.session_applied_revision);
    put32(out + 12, s.accepted_frames);
    put32(out + 16, s.rejected_frames);
    put32(out + 20, s.processed_frames);
  }
  put32(out + 24, HB_MUSE_RUNTIME_SESSION_MS);
  out[28] = (uint8_t)HB_MUSE_RUNTIME_FRAME_MAX;
  out[29] = (uint8_t)(HB_MUSE_RUNTIME_FRAME_MAX >> 8);
  out[30] = HB_MUSE_RUNTIME_QUEUE_SLOTS;
}
hb_muse_rpc_result_t hb_muse_rpc_execute(hb_muse_rpc_t *rpc,
                                         uint16_t cmd,
                                         const uint8_t *in,
                                         size_t len,
                                         uint8_t *out,
                                         size_t cap,
                                         size_t *out_len) {
  if (out_len)
    *out_len = 0;
  if (!rpc || !rpc->initialized || !in || !out || !out_len || cap < HB_MUSE_RUNTIME_FRAME_MAX)
    return HB_MUSE_RPC_INVALID;
  if (cmd == HB_MUSE_RPC_PULL_REPLAY) {
    if (len != 5 || in[0] != HB_MUSE_RPC_VERSION || cap < HB_MUSE_RUNTIME_FRAME_MAX + 4u)
      return HB_MUSE_RPC_INVALID;
    uint32_t seq =
        (uint32_t)in[1] | (uint32_t)in[2] << 8 | (uint32_t)in[3] << 16 | (uint32_t)in[4] << 24;
    hb_muse_runtime_status_t s = {0};
    if (!rpc->ops.status(rpc->user, &s) || !s.enabled || (!s.cloud_authorized && rpc->pull_size)) {
      clear_pull(rpc);
      return HB_MUSE_RPC_BUSY;
    }
    if (rpc->pull_size && rpc->pull_revision != s.session_requested_revision) {
      clear_pull(rpc);
      return HB_MUSE_RPC_BUSY;
    }
    if (!seq || (seq != rpc->pull_sequence &&
                 (rpc->pull_sequence == UINT32_MAX || seq != rpc->pull_sequence + 1u)))
      return HB_MUSE_RPC_INVALID;
    if (seq != rpc->pull_sequence) {
      memset(rpc->pull_cache, 0, sizeof rpc->pull_cache);
      size_t bytes = 0;
      /* Startup may need several owner ticks to grant access. Return
       * numbered empty results meanwhile, never dequeue before grant. */
      bool got = s.cloud_authorized &&
                 rpc->ops.take_reply(rpc->user, rpc->pull_cache, sizeof rpc->pull_cache, &bytes);
      if (bytes > sizeof rpc->pull_cache) {
        clear_pull(rpc);
        rpc->ops.revoke(rpc->user);
        return HB_MUSE_RPC_ERROR;
      }
      rpc->pull_size = got ? bytes : 0;
      rpc->pull_sequence = seq;
      rpc->pull_revision = s.session_requested_revision;
    }
    put32(out, seq);
    memcpy(out + 4, rpc->pull_cache, rpc->pull_size);
    *out_len = 4u + rpc->pull_size;
    return HB_MUSE_RPC_OK;
  }
  if (cmd < HB_MUSE_RPC_ENABLE || cmd > HB_MUSE_RPC_PULL)
    return HB_MUSE_RPC_UNSUPPORTED;
  if (cmd == HB_MUSE_RPC_TX) {
    hb_muse_wire_frame_t frame;
    if (!hb_muse_wire_decode(in, len, &frame) || frame.flags)
      return HB_MUSE_RPC_INVALID;
    hb_muse_runtime_status_t s;
    if (!rpc->ops.status(rpc->user, &s))
      return HB_MUSE_RPC_BUSY;
    return rpc->ops.submit(rpc->user, in, len) ? HB_MUSE_RPC_OK : HB_MUSE_RPC_BUSY;
  }
  if (len != (cmd == HB_MUSE_RPC_ENABLE ? 2u : 1u) || in[0] != HB_MUSE_RPC_VERSION)
    return HB_MUSE_RPC_INVALID;
  switch (cmd) {
    case HB_MUSE_RPC_ENABLE:
      if (in[1] > 1u)
        return HB_MUSE_RPC_INVALID;
      clear_pull(rpc);
      if (!in[1]) {
        rpc->ops.revoke(rpc->user);
        return HB_MUSE_RPC_OK;
      }
      if (!rpc->ops.ensure_initialized(rpc->user))
        return HB_MUSE_RPC_ERROR;
      return rpc->ops.enable(rpc->user, true) ? HB_MUSE_RPC_OK : HB_MUSE_RPC_BUSY;
    case HB_MUSE_RPC_CONNECT:
      return rpc->ops.connect(rpc->user) ? HB_MUSE_RPC_OK : HB_MUSE_RPC_BUSY;
    case HB_MUSE_RPC_REVOKE:
      clear_pull(rpc);
      rpc->ops.revoke(rpc->user);
      return HB_MUSE_RPC_OK;
    case HB_MUSE_RPC_STATUS:
      status_bytes(rpc, out);
      *out_len = HB_MUSE_RPC_STATUS_BYTES;
      return HB_MUSE_RPC_OK;
    case HB_MUSE_RPC_PULL: {
      if (rpc->pull_sequence)
        return HB_MUSE_RPC_INVALID; /* no mixed consumers */
      hb_muse_runtime_status_t s;
      if (!rpc->ops.status(rpc->user, &s))
        return HB_MUSE_RPC_BUSY;
      if (!rpc->ops.take_reply(rpc->user, out, cap, out_len)) {
        *out_len = 0;
        return HB_MUSE_RPC_OK;
      }
      if (*out_len > HB_MUSE_RUNTIME_FRAME_MAX) {
        *out_len = 0;
        rpc->ops.revoke(rpc->user);
        return HB_MUSE_RPC_ERROR;
      }
      return HB_MUSE_RPC_OK;
    }
    default:
      return HB_MUSE_RPC_UNSUPPORTED;
  }
}
