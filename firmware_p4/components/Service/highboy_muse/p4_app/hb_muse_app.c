#include "hb_muse_app.h"
#include "../transport/hb_muse_endpoint.h" /* shared bounded STATUS/TEXT contract */
#include <string.h>

static uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static void put32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(n >> (8 * i));
}
static void wipe(void *p, size_t n) {
  volatile uint8_t *b = p;
  while (n--)
    *b++ = 0;
}
static bool same(hb_muse_turn_id_t a, hb_muse_turn_id_t b) {
  return a.generation == b.generation && a.turn == b.turn;
}
static void mic_stop(hb_muse_app_t *a) {
  if (a->mic_active)
    a->ops.mic_stop(a->user);
  a->mic_active = false;
}
static void reset_text(hb_muse_app_t *a) {
  wipe(a->text, sizeof(a->text));
  a->text_offset = a->text_total = 0;
  a->text_revision = a->message_index = 0;
  a->text_complete = a->text_truncated = false;
}
static void revoke(hb_muse_app_t *a) {
  mic_stop(a);
  wipe(a->pcm, sizeof(a->pcm));
  a->pcm_count = a->pcm_offset = 0;
  a->begin_needed = a->end_needed = a->ended = a->terminal = a->turn_active = false;
  a->sdk_ready = a->connecting = false;
  hb_muse_wire_client_abandon(&a->wire);
  if (a->service_requested) {
    a->service_requested = false;
    (void)a->ops.control(a->user, HB_MUSE_APP_REVOKE);
  }
}
static void fail(hb_muse_app_t *a, hb_muse_app_error_t error) {
  revoke(a);
  (void)hb_muse_connection_reset(&a->core);
  a->error = error;
  a->phase = HB_MUSE_APP_ERROR;
}
static bool clock_ok(hb_muse_app_t *a, uint64_t now) {
  if (!a || !a->initialized)
    return false;
  if (a->clock_started && now < a->now_ms) {
    fail(a, HB_MUSE_APP_LINK_FAILED);
    return false;
  }
  a->clock_started = true;
  a->now_ms = now;
  return true;
}
static hb_muse_io_result_t backend_begin(void *user, hb_muse_turn_id_t id) {
  hb_muse_app_t *a = user;
  if (a->turn_active || !a->sdk_ready || !a->connecting || a->wire.state != HB_MUSE_CLIENT_ACTIVE)
    return HB_MUSE_IO_WOULD_BLOCK;
  a->turn = id;
  a->turn_active = true;
  a->begin_needed = true;
  a->end_needed = a->ended = a->terminal = false;
  a->acknowledged = a->pcm_first = a->end_total = 0;
  a->pcm_count = a->pcm_offset = 0;
  reset_text(a);
  a->phase = HB_MUSE_APP_STARTING;
  return HB_MUSE_IO_ACCEPTED;
}
static hb_muse_io_result_t
backend_audio(void *user, hb_muse_turn_id_t id, uint32_t first, const int16_t *p, size_t n) {
  hb_muse_app_t *a = user;
  if (!same(id, a->turn) || !a->turn_active || !a->mic_active || !n || n > HB_MUSE_PCM_MAX_FRAMES)
    return HB_MUSE_IO_FAILED;
  if (a->pcm_count)
    return HB_MUSE_IO_WOULD_BLOCK;
  if (first > HB_MUSE_APP_MAX_FRAMES || n > HB_MUSE_APP_MAX_FRAMES - first)
    return HB_MUSE_IO_FAILED;
  memcpy(a->pcm, p, n * sizeof(*p));
  a->pcm_first = first;
  a->pcm_count = (uint16_t)n;
  a->pcm_offset = 0;
  return HB_MUSE_IO_ACCEPTED;
}
static hb_muse_io_result_t backend_end(void *user, hb_muse_turn_id_t id, uint32_t total) {
  hb_muse_app_t *a = user;
  mic_stop(a);
  if (!a->turn_active || !same(id, a->turn) || !total)
    return HB_MUSE_IO_FAILED;
  a->end_total = total;
  a->end_needed = true;
  a->phase = HB_MUSE_APP_SENDING;
  return HB_MUSE_IO_ACCEPTED;
}
static void backend_cancel(void *user, hb_muse_turn_id_t id) {
  (void)id;
  revoke(user);
}
bool hb_muse_app_init(hb_muse_app_t *a, const hb_muse_app_ops_t *ops, void *user) {
  if (!a || !ops || !ops->control || !ops->send || !ops->mic_start || !ops->mic_stop)
    return false;
  memset(a, 0, sizeof(*a));
  a->ops = *ops;
  a->user = user;
  a->phase = HB_MUSE_APP_DISABLED;
  const hb_muse_backend_t backend = {backend_begin, backend_audio, backend_end, backend_cancel};
  a->initialized = hb_muse_init(&a->core, &backend, a) &&
                   hb_muse_wire_client_init(&a->wire, HB_MUSE_WIRE_DEFAULT_LEASE_MS);
  return a->initialized;
}
bool hb_muse_app_confirm(hb_muse_app_t *a, uint64_t now) {
  if (!clock_ok(a, now) || a->enabled)
    return false;
  a->armed = false;
  (void)hb_muse_ptt(&a->core, true);
  (void)hb_muse_set_enabled(&a->core, true);
  a->enabled = true;
  a->service_requested = true;
  if (!a->ops.control(a->user, HB_MUSE_APP_ENABLE)) {
    fail(a, HB_MUSE_APP_CONTROL_FAILED);
    return false;
  }
  a->error = HB_MUSE_APP_NO_ERROR;
  a->phase = HB_MUSE_APP_OFFLINE;
  return true;
}
bool hb_muse_app_connect(hb_muse_app_t *a, uint64_t nonce, uint64_t now) {
  if (!clock_ok(a, now) || !a->enabled || a->connecting || !nonce)
    return false;
  /* Confirm/connect presses cannot become PTT; wait for a real release. */
  a->armed = false;
  (void)hb_muse_ptt(&a->core, true);
  a->service_requested = true;
  if (!a->ops.control(a->user, HB_MUSE_APP_CONNECT)) {
    fail(a, HB_MUSE_APP_CONTROL_FAILED);
    return false;
  }
  if (!hb_muse_wire_client_prepare_hello(&a->wire, nonce, now)) {
    fail(a, HB_MUSE_APP_LINK_FAILED);
    return false;
  }
  a->connecting = true;
  a->error = HB_MUSE_APP_NO_ERROR;
  a->phase = HB_MUSE_APP_CONNECTING;
  return true;
}
hb_muse_result_t hb_muse_app_physical_ptt(hb_muse_app_t *a, bool pressed, uint64_t now) {
  if (!clock_ok(a, now))
    return HB_MUSE_INVALID_ARGUMENT;
  if (!pressed) {
    mic_stop(a);
    a->armed = true;
  }
  if (pressed && !a->armed)
    return HB_MUSE_NOT_READY;
  hb_muse_result_t r = hb_muse_ptt(&a->core, pressed);
  if (!a->connecting && a->enabled && a->phase != HB_MUSE_APP_ERROR) {
    (void)hb_muse_connection_reset(&a->core);
    a->phase = HB_MUSE_APP_OFFLINE;
  }
  return r;
}
void hb_muse_app_close(hb_muse_app_t *a) {
  if (!a || !a->initialized)
    return;
  mic_stop(a);
  (void)hb_muse_set_enabled(&a->core, false);
  revoke(a);
  a->enabled = a->armed = false;
  a->phase = HB_MUSE_APP_DISABLED;
  reset_text(a);
}
void hb_muse_app_cancel(hb_muse_app_t *a) {
  if (!a || !a->initialized)
    return;
  mic_stop(a);
  (void)hb_muse_cancel(&a->core);
  revoke(a);
  (void)hb_muse_connection_reset(&a->core);
  a->phase = a->enabled ? HB_MUSE_APP_OFFLINE : HB_MUSE_APP_DISABLED;
}
size_t hb_muse_app_pcm_capacity(const hb_muse_app_t *a) {
  if (!a || !a->initialized || !a->mic_active || a->pcm_count ||
      !hb_muse_capture_allowed(&a->core, a->turn))
    return 0;
  uint32_t left = HB_MUSE_APP_MAX_FRAMES - a->core.accepted_frames;
  return left < HB_MUSE_PCM_MAX_FRAMES ? left : HB_MUSE_PCM_MAX_FRAMES;
}
hb_muse_result_t hb_muse_app_pcm(hb_muse_app_t *a, const int16_t *pcm, size_t frames) {
  if (!a || !a->initialized)
    return HB_MUSE_INVALID_ARGUMENT;
  if (!a->mic_active)
    return HB_MUSE_NOT_READY;
  hb_muse_result_t r = hb_muse_offer_pcm(&a->core, a->turn, a->core.accepted_frames, pcm, frames);
  if (r == HB_MUSE_BACKEND_ERROR)
    fail(a, HB_MUSE_APP_PEER_FAILED);
  else if (r == HB_MUSE_OK && a->core.accepted_frames == HB_MUSE_APP_MAX_FRAMES) {
    /* Local duration limit is an enforced STOP, never a synthetic press.
     * Do not leave I2S recording after capacity reaches zero. Rearming
     * still requires an actual physical release and a new press. */
    mic_stop(a);
    (void)hb_muse_ptt(&a->core, false);
    a->armed = false;
  }
  return r;
}
static bool command(hb_muse_app_t *a, unsigned op, const uint8_t *p, size_t n) {
  hb_muse_turn_id_t id = a->turn_active ? a->turn : (hb_muse_turn_id_t){0};
  return hb_muse_wire_client_prepare_command(
      &a->wire, (uint8_t)op, id.generation, id.turn, p, n, a->now_ms);
}
static bool status_valid(const uint8_t *p, size_t n) {
  if (n != 32 || p[0] > HB_MUSE_ENDPOINT_CANCELLED || p[2] || p[3] > 1 ||
      u32(p + 24) != HB_MUSE_APP_MAX_FRAMES || u16(p + 28) != 106 || u16(p + 30) != 16000 ||
      u32(p + 8) > u32(p + 4) || u32(p + 4) > HB_MUSE_APP_MAX_FRAMES ||
      (!u32(p + 12) && u16(p + 20)) || u16(p + 20) >= HB_MUSE_APP_TEXT_CAP ||
      u16(p + 22) > HB_MUSE_ADAPTER_CAPACITY)
    return false;
  return !(p[1] & 8u) || (p[1] & 7u) == 7u;
}
static bool utf8_text(char *text, size_t size) {
  /* Validate a COMPLETE page assembly before exposing it to LVGL. */
  for (size_t i = 0; i < size;) {
    uint8_t c = (uint8_t)text[i];
    unsigned continuation = 0;
    uint32_t point, min;
    if (c < 0x80) {
      if (!c)
        return false;
      if (c < 32 || c == 127)
        text[i] = ' ';
      ++i;
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) {
      continuation = 1;
      point = c & 31u;
      min = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      continuation = 2;
      point = c & 15u;
      min = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      continuation = 3;
      point = c & 7u;
      min = 0x10000;
    } else
      return false;
    if (continuation > size - i - 1)
      return false;
    for (unsigned j = 1; j <= continuation; ++j) {
      uint8_t b = (uint8_t)text[i + j];
      if ((b & 0xc0u) != 0x80u)
        return false;
      point = (point << 6) | (b & 63u);
    }
    if (point < min || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
      return false;
    i += (size_t)continuation + 1;
  }
  text[size] = '\0';
  return true;
}
static void on_status(hb_muse_app_t *a, const hb_muse_wire_frame_t *r) {
  const uint8_t *p = r->payload;
  if (!status_valid(p, r->payload_len)) {
    fail(a, HB_MUSE_APP_BAD_RESPONSE);
    return;
  }
  bool ready = (p[1] & 15u) == 15u;
  if (a->sdk_ready && !ready) {
    fail(a, HB_MUSE_APP_PEER_FAILED);
    return;
  }
  a->sdk_ready = ready;
  a->remote_phase = p[0];
  a->last_status_ms = a->now_ms;
  a->status_due = false;
  (void)hb_muse_link_update(
      &a->core, hb_muse_generation(&a->core), (p[1] & 2u) != 0, (p[1] & 4u) != 0, ready);
  if (!a->turn_active) {
    a->phase = ready ? HB_MUSE_APP_READY : HB_MUSE_APP_CONNECTING;
    return;
  }
  if (u32(p + 4) != a->acknowledged || u32(p + 4) > a->core.accepted_frames) {
    fail(a, HB_MUSE_APP_BAD_RESPONSE);
    return;
  }
  if (p[0] == HB_MUSE_ENDPOINT_FAILED || p[0] == HB_MUSE_ENDPOINT_CANCELLED) {
    fail(a, HB_MUSE_APP_PEER_FAILED);
    return;
  }
  if (p[0] == HB_MUSE_ENDPOINT_LISTENING && (p[1] & 16u) &&
      hb_muse_capture_allowed(&a->core, a->turn) && !a->mic_active) {
    if (!a->ops.mic_start(a->user, HB_MUSE_PCM_RATE_HZ)) {
      a->ops.mic_stop(a->user);
      fail(a, HB_MUSE_APP_MIC_FAILED);
      return;
    }
    a->mic_active = true;
    a->phase = HB_MUSE_APP_LISTENING;
  }
  uint32_t revision = u32(p + 12), message = u32(p + 16);
  uint16_t total = u16(p + 20);
  if (revision != a->text_revision || message != a->message_index || total != a->text_total) {
    wipe(a->text, sizeof(a->text));
    a->text_offset = 0;
    a->text_complete = false;
    a->text_revision = revision;
    a->message_index = message;
    a->text_total = total;
  }
  a->text_truncated = p[3] != 0;
  if (p[0] == HB_MUSE_ENDPOINT_DONE) {
    if (!a->ended || !(p[1] & 128u)) {
      fail(a, HB_MUSE_APP_BAD_RESPONSE);
      return;
    }
    a->terminal = true;
    mic_stop(a);
  }
}
static void on_text(hb_muse_app_t *a, const hb_muse_wire_frame_t *r) {
  const uint8_t *p = r->payload;
  size_t n = r->payload_len;
  if (n < 12 || a->text_offset > a->text_total || a->text_total >= sizeof(a->text) ||
      u32(p) != a->text_revision || u32(p + 4) != a->message_index || u16(p + 8) != a->text_total ||
      u16(p + 10) != a->text_offset || n - 12 > (size_t)(a->text_total - a->text_offset) ||
      (n == 12 && a->text_offset < a->text_total)) {
    fail(a, HB_MUSE_APP_BAD_RESPONSE);
    return;
  }
  memcpy(a->text + a->text_offset, p + 12, n - 12);
  a->text_offset += (uint16_t)(n - 12);
  if (a->text_offset == a->text_total) {
    if (!utf8_text(a->text, a->text_total)) {
      fail(a, HB_MUSE_APP_BAD_RESPONSE);
      return;
    }
    a->text_complete = true;
  }
}
void hb_muse_app_receive(hb_muse_app_t *a, const uint8_t *frame, size_t n, uint64_t now) {
  if (!clock_ok(a, now) || !a->connecting)
    return;
  hb_muse_wire_frame_t previous = a->wire.request, r;
  hb_muse_client_accept_t accepted =
      hb_muse_wire_client_accept_response(&a->wire, frame, n, now, &r);
  if (accepted == HB_MUSE_CLIENT_IGNORED)
    return;
  if (accepted == HB_MUSE_CLIENT_LINK_LOST) {
    fail(a, HB_MUSE_APP_LINK_FAILED);
    return;
  }
  if (r.status != HB_MUSE_WIRE_OK) {
    if (r.opcode == HB_MUSE_WIRE_PCM && r.status == HB_MUSE_WIRE_BLOCKED) {
      a->status_due = true;
      return;
    }
    if (r.opcode == HB_MUSE_WIRE_TEXT && r.status == HB_MUSE_WIRE_BLOCKED) {
      wipe(a->text, sizeof(a->text));
      a->text_offset = 0;
      a->text_complete = false;
      a->status_due = true;
      return;
    }
    fail(a, HB_MUSE_APP_PEER_FAILED);
    return;
  }
  switch (r.opcode) {
    case HB_MUSE_WIRE_HELLO:
      if (!hb_muse_wire_client_prepare_open(&a->wire, now))
        fail(a, HB_MUSE_APP_LINK_FAILED);
      break;
    case HB_MUSE_WIRE_OPEN:
      a->status_due = true;
      break;
    case HB_MUSE_WIRE_BEGIN:
      a->begin_needed = false;
      a->status_due = true;
      break;
    case HB_MUSE_WIRE_STATUS:
      on_status(a, &r);
      break;
    case HB_MUSE_WIRE_PCM: {
      size_t count = ((size_t)previous.payload_len - 4u) / 2u;
      if (r.payload_len || count > (size_t)(a->pcm_count - a->pcm_offset) ||
          u32(previous.payload) != a->acknowledged) {
        fail(a, HB_MUSE_APP_BAD_RESPONSE);
        break;
      }
      wipe(a->pcm + a->pcm_offset, count * sizeof(a->pcm[0]));
      a->pcm_offset += (uint16_t)count;
      a->acknowledged += (uint32_t)count;
      if (a->pcm_offset == a->pcm_count) {
        a->pcm_offset = a->pcm_count = 0;
      }
      break;
    }
    case HB_MUSE_WIRE_END:
      if (r.payload_len) {
        fail(a, HB_MUSE_APP_BAD_RESPONSE);
        break;
      }
      a->end_needed = false;
      a->ended = true;
      a->phase = HB_MUSE_APP_WAITING;
      a->status_due = true;
      break;
    case HB_MUSE_WIRE_TEXT:
      on_text(a, &r);
      break;
    case HB_MUSE_WIRE_RETIRE:
      if (r.payload_len || !a->terminal) {
        fail(a, HB_MUSE_APP_BAD_RESPONSE);
        break;
      }
      (void)hb_muse_backend_event(&a->core, a->turn, HB_MUSE_EVENT_TURN_DONE);
      a->turn_active = a->terminal = false;
      a->turn = (hb_muse_turn_id_t){0};
      a->phase = HB_MUSE_APP_REPLY;
      break;
    default:
      fail(a, HB_MUSE_APP_BAD_RESPONSE);
      break;
  }
}
void hb_muse_app_pump(hb_muse_app_t *a, uint64_t now) {
  if (!clock_ok(a, now) || !a->connecting)
    return;
  hb_muse_wire_client_tick(&a->wire, now);
  if (a->wire.fault != HB_MUSE_CLIENT_FAULT_NONE) {
    fail(a, HB_MUSE_APP_LINK_FAILED);
    return;
  }
  if (!a->wire.pending && a->wire.state == HB_MUSE_CLIENT_ACTIVE) {
    uint8_t payload[216];
    size_t n = 0;
    unsigned opcode = 0;
    if (a->begin_needed)
      opcode = HB_MUSE_WIRE_BEGIN;
    else if (a->status_due || now - a->last_status_ms >= HB_MUSE_APP_STATUS_MS)
      opcode = HB_MUSE_WIRE_STATUS;
    else if (a->pcm_count) {
      size_t count = (size_t)(a->pcm_count - a->pcm_offset);
      if (count > 106)
        count = 106;
      put32(payload, a->pcm_first + a->pcm_offset);
      n = 4 + count * 2;
      for (size_t i = 0; i < count; ++i) {
        uint16_t v = (uint16_t)a->pcm[a->pcm_offset + i];
        payload[4 + 2 * i] = (uint8_t)v;
        payload[5 + 2 * i] = (uint8_t)(v >> 8);
      }
      opcode = HB_MUSE_WIRE_PCM;
    } else if (a->end_needed) {
      put32(payload, a->end_total);
      n = 4;
      opcode = HB_MUSE_WIRE_END;
    } else if (a->text_revision && !a->text_complete) {
      put32(payload, a->text_revision);
      payload[4] = (uint8_t)a->text_offset;
      payload[5] = (uint8_t)(a->text_offset >> 8);
      n = 6;
      opcode = HB_MUSE_WIRE_TEXT;
    } else if (a->terminal && (a->text_complete || !a->text_revision))
      opcode = HB_MUSE_WIRE_RETIRE;
    if (opcode && !command(a, opcode, payload, n)) {
      wipe(payload, sizeof(payload));
      fail(a, HB_MUSE_APP_LINK_FAILED);
      return;
    }
    wipe(payload, sizeof(payload));
  }
  uint8_t bytes[248];
  size_t length = 0;
  if (hb_muse_wire_client_next_request(&a->wire, now, bytes, sizeof(bytes), &length))
    (void)a->ops.send(a->user, bytes, length);
  wipe(bytes, sizeof(bytes));
}
bool hb_muse_app_view(const hb_muse_app_t *a, hb_muse_app_view_t *out) {
  if (!a || !a->initialized || !out)
    return false;
  *out = (hb_muse_app_view_t){.phase = a->phase,
                              .error = a->error,
                              .enabled = a->enabled,
                              .sdk_ready = a->sdk_ready,
                              .mic_active = a->mic_active,
                              .text_complete = a->text_complete,
                              .text_truncated = a->text_truncated,
                              .accepted_frames = a->core.accepted_frames,
                              .acknowledged_frames = a->acknowledged,
                              .text_revision = a->text_revision,
                              .message_index = a->message_index,
                              .text_bytes = a->text_complete ? a->text_total : 0,
                              .text = a->text_complete ? a->text : ""};
  return true;
}
