/* P4 native Muse owner. No SDK, radio initialization, pairing credentials or
 * cloud sockets. I2S and SPI calls run only here, never in the LVGL thread.
 * Old I2S API can block 300 ms: release is checked again before any sample is
 * accepted. BACK/release may delay hardware-stop, but never upload late audio.
 */
#include "hb_muse_p4_service.h"
#include "hb_muse_app.h"
#ifndef HB_MUSE_P4_HOST_TEST
#include "audio_i2s.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "power_manager.h"
#include "spi_bridge.h"
#include "sys_prio.h"
#endif
#include <stdio.h>
#include <string.h>

#define LOAD(x)     __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)
#define RPC_MS      80u
#define FRAME_MAX   248u
#define EDGE_SLOTS  8u
enum {
  RPC_ENABLE = 0x1101,
  RPC_CONNECT,
  RPC_REVOKE,
  RPC_STATUS,
  RPC_TX,
  RPC_PULL,
  RPC_SETUP_OPEN,
  RPC_SETUP_STATUS,
  RPC_SETUP_EDGE,
  RPC_SETUP_CLOSE,
  RPC_TOKEN
};
typedef struct {
  bool pressed;
  uint64_t serial, ticket;
} key_edge_t;
typedef struct {
  hb_muse_app_t app;
  hb_muse_p4_view_t view;
  uint32_t generation, mic_edge;
  hb_muse_p4_operation_t operation;
  bool mic, held;
  bool recording, recorder_live, capture_fault, release_pending;
  uint32_t recorded, drained;
  int16_t *recording_pcm;
  int16_t pcm[320];
  uint8_t response[FRAME_MAX + 4];
} owner_t;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static hb_muse_p4_view_t s_view;
static key_edge_t s_edges[EDGE_SLOTS];
static unsigned s_read, s_count;
static bool s_running, s_requested, s_held, s_shutdown;
static uint32_t s_generation, s_release;
static uint64_t s_edge_serial;
bool hb_muse_p4_is_active(void) {
  return LOAD(s_running);
}

static uint64_t now(void) {
  return (uint64_t)esp_timer_get_time() / 1000u;
}
static bool current(const owner_t *o) {
  return LOAD(s_requested) && LOAD(s_generation) == o->generation;
}
static void publish(owner_t *o) {
  portENTER_CRITICAL(&s_lock);
  if (s_generation == o->generation)
    s_view = o->view;
  portEXIT_CRITICAL(&s_lock);
}
void hb_muse_p4_view(hb_muse_p4_view_t *out) {
  if (!out)
    return;
  portENTER_CRITICAL(&s_lock);
  *out = s_view;
  portEXIT_CRITICAL(&s_lock);
}
static void message(owner_t *o, bool error, const char *status, const char *detail) {
  o->view.error = error;
  snprintf(o->view.status, sizeof o->view.status, "%s", status);
  snprintf(o->view.detail, sizeof o->view.detail, "%s", detail);
  publish(o);
}
void hb_muse_p4_stop(void) {
  STORE(s_requested, false);
  STORE(s_held, false);
  __atomic_add_fetch(&s_release, 1, __ATOMIC_ACQ_REL);
  portENTER_CRITICAL(&s_lock);
  s_view.ready = false;
  s_view.confirming = false;
  s_view.ticket = 0;
  /* mic remains factual until the owner disables I2S. */
  portEXIT_CRITICAL(&s_lock);
}
void hb_muse_p4_key(bool pressed) {
  portENTER_CRITICAL(&s_lock);
  if (s_requested && s_running && s_held != pressed) {
    STORE(s_held, pressed);
    if (!pressed)
      __atomic_add_fetch(&s_release, 1, __ATOMIC_ACQ_REL);
    if (s_edge_serial == UINT64_MAX || s_count == EDGE_SLOTS) {
      STORE(s_requested, false);
      STORE(s_held, false);
      __atomic_add_fetch(&s_release, 1, __ATOMIC_ACQ_REL);
    } else {
      unsigned at = (s_read + s_count) % EDGE_SLOTS;
      s_edges[at] = (key_edge_t){pressed, ++s_edge_serial, s_view.ticket};
      ++s_count;
    }
  }
  portEXIT_CRITICAL(&s_lock);
}
static bool pop(key_edge_t *edge) {
  bool yes = false;
  portENTER_CRITICAL(&s_lock);
  if (s_count) {
    *edge = s_edges[s_read];
    memset(&s_edges[s_read], 0, sizeof *edge);
    s_read = (s_read + 1) % EDGE_SLOTS;
    --s_count;
    yes = true;
  }
  portEXIT_CRITICAL(&s_lock);
  return yes;
}
static bool rpc(owner_t *o, uint16_t command, const void *input, size_t size, size_t *received) {
  spi_header_t h = {0};
  if (size > FRAME_MAX)
    return false;
  memset(o->response, 0, sizeof o->response);
  esp_err_t err = spi_bridge_send_command(
      (spi_id_t)command, input, (uint8_t)size, &h, o->response, sizeof o->response, RPC_MS);
  if (received)
    *received = h.length;
  return err == ESP_OK && h.length <= sizeof o->response;
}
static bool control(void *user, hb_muse_app_control_t action) {
  owner_t *o = user;
  uint8_t args[2] = {1, 1};
  size_t n = 0;
  uint16_t cmd = action == HB_MUSE_APP_ENABLE    ? RPC_ENABLE
                 : action == HB_MUSE_APP_CONNECT ? RPC_CONNECT
                                                 : RPC_REVOKE;
  if (action != HB_MUSE_APP_REVOKE && !current(o))
    return false;
  return rpc(o, cmd, args, action == HB_MUSE_APP_ENABLE ? 2 : 1, &n) && n == 0;
}
static bool send_frame(void *user, const uint8_t *frame, size_t size) {
  owner_t *o = user;
  size_t n = 0;
  return current(o) && rpc(o, RPC_TX, frame, size, &n) && n == 0;
}
static void recorder(void *arg) {
  owner_t *o = arg;
  int16_t block[320];
  while (LOAD(o->recording) && current(o) && LOAD(s_held) && LOAD(s_release) == o->mic_edge) {
    uint32_t used = LOAD(o->recorded);
    size_t want = HB_MUSE_APP_MAX_FRAMES - used;
    if (want > 320)
      want = 320;
    if (!want)
      break;
    int got = audio_i2s_mic_stream_read(block, (int)want);
    /* Never publish a read straddling release/cancel or a new press. */
    if (!LOAD(o->recording) || !current(o) || !LOAD(s_held) || LOAD(s_release) != o->mic_edge)
      break;
    if (got <= 0 || got > (int)want) {
      STORE(o->capture_fault, true);
      break;
    }
    memcpy(o->recording_pcm + used, block, (size_t)got * sizeof *block);
    STORE(o->recorded, used + (uint32_t)got);
  }
  memset(block, 0, sizeof block);
  audio_i2s_mic_stream_stop();
  STORE(o->recording, false);
  /* Last owner access; cleanup cannot free the buffer before this store. */
  STORE(o->recorder_live, false);
  vTaskDelete(NULL);
}
static void mic_stop(void *user) {
  owner_t *o = user;
  STORE(o->recording, false);
  while (LOAD(o->recorder_live))
    vTaskDelay(pdMS_TO_TICKS(1));
  o->mic = false;
  o->view.mic = false;
  o->view.level = 0;
}
static bool mic_start(void *user, uint32_t rate) {
  owner_t *o = user;
  if (rate != 16000 || !current(o) || !LOAD(s_held))
    return false;
  uint32_t release = LOAD(s_release);
  if (!o->recording_pcm)
    o->recording_pcm = heap_caps_calloc(
        HB_MUSE_APP_MAX_FRAMES, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!o->recording_pcm)
    return false;
  o->recorded = o->drained = 0;
  o->release_pending = false;
  o->capture_fault = false;
  bool ok = audio_i2s_mic_stream_start(rate) == ESP_OK;
  o->mic = ok;
  if (!ok || !current(o) || !LOAD(s_held) || release != LOAD(s_release)) {
    if (ok)
      audio_i2s_mic_stream_stop();
    o->mic = false;
    return false;
  }
  o->mic_edge = release;
  STORE(o->recording, true);
  STORE(o->recorder_live, true);
  if (xTaskCreatePinnedToCore(recorder, "muse_mic", 3072, o, SYS_PRIO_SERVICE_LO + 1, NULL, 1) !=
      pdPASS) {
    STORE(o->recorder_live, false);
    STORE(o->recording, false);
    audio_i2s_mic_stream_stop();
    o->mic = false;
    return false;
  }
  return true;
}
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t le64(const uint8_t *p) {
  return le32(p) | (uint64_t)le32(p + 4) << 32;
}
static void put64(uint8_t *p, uint64_t n) {
  for (unsigned i = 0; i < 8; ++i)
    p[i] = (uint8_t)(n >> (i * 8));
}
static bool pull_reply(owner_t *o, uint32_t sequence, size_t *n) {
  uint8_t request[5] = {1};
  for (unsigned i = 0; i < 4; ++i)
    request[i + 1] = (uint8_t)(sequence >> (8 * i));
  /* A versioned cached read, never a retry of legacy consuming PULL/TX.
   * C5 returns the exact prior result until the next sequence acknowledges it. */
  for (unsigned attempt = 0; attempt < 3 && current(o); ++attempt) {
    if (rpc(o, 0x110c, request, sizeof request, n)) {
      if (*n < 4 || le32(o->response) != sequence)
        return false;
      *n -= 4;
      memmove(o->response, o->response + 4, *n);
      return true;
    }
    if (attempt < 2)
      vTaskDelay(pdMS_TO_TICKS(10u * (attempt + 1u)));
  }
  return false;
}
/* Existing NVS is already initialized by TentacleOS. Monotonic durable nonce;
 * fail closed, never erase/migrate/reinitialize NVS or read any user secret. */
static bool next_nonce(uint64_t *out) {
  nvs_handle_t handle;
  uint64_t value = 0, check = 0;
  if (nvs_open("muse_p4", NVS_READWRITE, &handle) != ESP_OK)
    return false;
  esp_err_t err = nvs_get_u64(handle, "nonce", &value);
  bool ok = (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) && value < UINT64_MAX;
  if (ok) {
    ++value;
    ok = nvs_set_u64(handle, "nonce", value) == ESP_OK && nvs_commit(handle) == ESP_OK &&
         nvs_get_u64(handle, "nonce", &check) == ESP_OK && check == value;
  }
  nvs_close(handle);
  if (ok)
    *out = value;
  return ok;
}
static bool prepared(owner_t *o) {
  uint64_t deadline = now() + 10000;
  uint8_t v = 1;
  while (current(o) && now() < deadline) {
    size_t n = 0;
    if (!rpc(o, RPC_STATUS, &v, 1, &n) || n != 32 || o->response[0] != 1 || o->response[31] != 0 ||
        le32(o->response + 24) != 120000 || o->response[28] != 248 || o->response[29] != 0 ||
        o->response[30] != 4) {
      message(o, true, "C5 link unavailable", "Check C5 / retry manually");
      return false;
    }
    uint8_t phase = o->response[1];
    if (phase == 4 && (o->response[3] & 7) == 7)
      return true;
    if (phase == 2) {
      message(o, true, "Pairing required", "LEFT: pair with Muse app");
      return false;
    }
    if (phase == 3) {
      message(o, true, "Wi-Fi disconnected", "Connect via Wi-Fi menu");
      return false;
    }
    if (phase == 9 || o->response[2]) {
      message(o, true, "C5 Muse unavailable", "C5 init/storage error");
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (current(o))
    message(o, true, "Preparation timed out", "RIGHT: retry manually");
  return false;
}
static void app_publish(owner_t *o) {
  hb_muse_app_view_t v;
  hb_muse_app_view(&o->app, &v);
  static const char *const names[] = {"Offline",
                                      "Offline",
                                      "Connecting",
                                      "Ready",
                                      "Starting turn",
                                      "Listening",
                                      "Sending voice note",
                                      "Waiting for reply",
                                      "Reply",
                                      "Session error"};
  o->view.phase = (uint8_t)v.phase;
  o->view.ready = v.sdk_ready;
  o->view.mic = o->mic;
  o->view.error = v.error != HB_MUSE_APP_NO_ERROR;
  static const char *const errors[] = {"",
                                       "C5 control failed",
                                       "SPI link lost",
                                       "Invalid C5 reply",
                                       "Microphone failed",
                                       "Muse reply unavailable"};
  snprintf(o->view.status, sizeof o->view.status, "%s", v.error ? errors[v.error] : names[v.phase]);
  const char *detail = v.error        ? "Mic off / RIGHT: reconnect. See C5 log for cause."
                       : o->mic       ? "Mic on / release OK to send"
                       : v.mic_active ? "Mic off / sending recorded audio"
                       : v.phase == HB_MUSE_APP_WAITING ? "Waiting for Muse..."
                       : v.phase == HB_MUSE_APP_REPLY   ? "UP/DOWN: read reply / hold OK: new note"
                       : v.sdk_ready ? "Mic off / hold OK for a voice note (15 s max)"
                                     : "Mic off / waiting for verified SDK readiness";
  snprintf(o->view.detail, sizeof o->view.detail, "%s", detail);
  /* Keep the last complete reply through capture, transfer and link errors.
   * Incomplete/new text must never replace a readable previous reply. */
  if (v.text_complete)
    snprintf(o->view.reply, sizeof o->view.reply, "%s", v.text);
  publish(o);
}
static void capture(owner_t *o, size_t capacity) {
  size_t count = LOAD(o->recorded) - o->drained;
  if (count > capacity)
    count = capacity;
  if (count > 320)
    count = 320;
  if (!count || !current(o))
    return;
  memcpy(o->pcm, o->recording_pcm + o->drained, count * sizeof(int16_t));
  if (hb_muse_app_pcm(&o->app, o->pcm, count) != HB_MUSE_OK) {
    hb_muse_app_cancel(&o->app);
  } else {
    uint32_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
      int32_t x = o->pcm[i];
      uint32_t a = (uint32_t)(x < 0 ? -x : x);
      if (a > peak)
        peak = a;
    }
    o->view.level = (uint8_t)(peak * 100 / 32768);
    memset(o->recording_pcm + o->drained, 0, count * sizeof(int16_t));
    o->drained += (uint32_t)count;
  }
  memset(o->pcm, 0, sizeof o->pcm);
}
static void chat_edges(owner_t *o) {
  key_edge_t edge;
  while (pop(&edge)) {
    if (!edge.pressed && o->app.mic_active) {
      /* Stop hardware now; drain only samples already published while
       * held before applying END to the serialized protocol owner. */
      mic_stop(o);
      o->release_pending = true;
    } else if (!o->release_pending)
      (void)hb_muse_app_physical_ptt(&o->app, edge.pressed, now());
  }
}
static void chat(owner_t *o) {
  hb_muse_app_ops_t ops = {control, send_frame, mic_start, mic_stop};
  hb_muse_app_init(&o->app, &ops, o);
  if (!hb_muse_app_confirm(&o->app, now())) {
    message(o, true, "Enable failed", "C5 did not acknowledge Muse enable.");
    return;
  }
  if (!prepared(o)) {
    hb_muse_app_close(&o->app);
    return;
  }
  uint64_t nonce;
  if (!next_nonce(&nonce)) {
    message(o, true, "Nonce storage failed", "Connection blocked");
    hb_muse_app_close(&o->app);
    return;
  }
  if (!hb_muse_app_connect(&o->app, nonce, now())) {
    app_publish(o);
    return;
  }
  uint64_t deadline = now() + 120000;
  uint32_t pull_sequence = 1;
  while (current(o) && now() < deadline) {
    chat_edges(o);
    if (!current(o))
      break;
    size_t n = 0;
    if (!pull_reply(o, pull_sequence, &n)) {
      hb_muse_app_cancel(&o->app);
      message(o, true, "SPI reply lost", "C5 read failed / mic off");
      break;
    }
    if (pull_sequence == UINT32_MAX) {
      hb_muse_app_cancel(&o->app);
      break;
    }
    ++pull_sequence;
    /* Process a release that happened while the SPI reply was blocking
     * BEFORE that reply could start the microphone. */
    chat_edges(o);
    if (!current(o))
      break;
    if (n)
      hb_muse_app_receive(&o->app, o->response, n, now());
    size_t capacity = hb_muse_app_pcm_capacity(&o->app);
    if (LOAD(o->capture_fault)) {
      hb_muse_app_cancel(&o->app);
      message(o, true, "Microphone read failed", "Recording stopped; no silent sample skipping.");
      break;
    }
    if (capacity)
      capture(o, capacity);
    if (o->release_pending && o->drained == LOAD(o->recorded)) {
      o->release_pending = false;
      (void)hb_muse_app_physical_ptt(&o->app, false, now());
    }
    o->mic = LOAD(o->recording);
    if (!current(o))
      break;
    hb_muse_app_pump(&o->app, now());
    app_publish(o);
    if (o->view.error)
      break;
    /* Idle polling was hundreds of RPCs/sec. Keep fast draining only
     * while actual PCM is queued; no automatic audio retransmission. */
    vTaskDelay(pdMS_TO_TICKS(o->app.pcm_count || o->drained < LOAD(o->recorded) ? 2 : 12));
  }
  hb_muse_app_close(&o->app);
  mic_stop(o);
}
static void setup(owner_t *o) {
  size_t n = 0;
  /* C5 deliberately refuses to arm UART over an already-loaded token.
   * Report that state as present, not as a failed setup. Read-only probe. */
  if (o->operation == HB_MUSE_P4_TOKEN && rpc(o, RPC_SETUP_STATUS, NULL, 0, &n) && n == 32 &&
      o->response[0] == 1 && o->response[1] <= 7 && (o->response[3] & 1)) {
    message(o,
            false,
            "SDK token already present",
            "No reload needed. RIGHT: connect saved account; LEFT: pair if needed.");
    return;
  }
  if (!current(o))
    return;
  uint64_t edge_ticket = 0;
  bool released = false;
  bool completed = false;
  if (!rpc(o, o->operation == HB_MUSE_P4_TOKEN ? RPC_TOKEN : RPC_SETUP_OPEN, NULL, 0, &n) ||
      n != 1) {
    message(o, true, "Setup link failed", "C5 reply missing or invalid. No automatic replay.");
    return;
  }
  if (o->response[0] != 1) {
    message(
        o, true, "C5 refused setup", "Stop other radio apps/session. Saved pairing is not erased.");
    return;
  }
  uint64_t deadline = now() + 120000;
  while (current(o) && now() < deadline) {
    /* Only the read-only status query may be repeated. Never replay OPEN,
     * token loading or physical approval. Invalidate consent UI/edges
     * while transport state is unknown; persistent failure still closes. */
    bool status_ok = false;
    for (unsigned attempt = 0; attempt < 3 && current(o) && now() < deadline; ++attempt) {
      if (rpc(o, RPC_SETUP_STATUS, NULL, 0, &n)) {
        status_ok = true;
        break;
      }
      o->view.confirming = false;
      o->view.ticket = 0;
      edge_ticket = 0;
      released = false;
      key_edge_t discarded;
      while (pop(&discarded)) {}
      message(o, false, "Checking pairing link", "Approval paused; BACK cancels.");
      if (attempt < 2)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!current(o))
      break;
    if (!status_ok || n != 32 || o->response[0] != 1 || o->response[1] > 7) {
      message(o, true, "Setup link failed", "No pairing claimed. Retry explicitly.");
      break;
    }
    uint8_t phase = o->response[1], flags = o->response[3];
    bool reserved = false;
    for (unsigned i = 24; i < 32; ++i)
      reserved |= o->response[i] != 0;
    if (reserved) {
      message(o, true, "Invalid setup status", "C5 protocol mismatch.");
      break;
    }
    o->view.ticket = phase == 4 ? le64(o->response + 12) : 0;
    if (o->view.ticket != edge_ticket) {
      edge_ticket = o->view.ticket;
      released = false;
    }
    o->view.confirming = phase == 4 && o->view.ticket != 0;
    o->view.phase = HB_MUSE_APP_OFFLINE; /* setup is never a chat/thinking state */
    if (o->operation == HB_MUSE_P4_TOKEN) {
      message(o,
              false,
              flags & 1 ? "SDK token present" : "Local token window",
              flags & 1 ? "Token remains on C5. LEFT: pair with Muse app."
                        : "Use the reviewed Mac-to-C5 USB loader. Never type a token on P4.");
      if (flags & 1) {
        completed = true;
        break;
      }
    } else {
      static const char *const status[] = {"Setup closed",
                                           "Stopping session",
                                           "Muse pairing available",
                                           "Phone connected",
                                           "Approve this pairing?",
                                           "Pairing accepted",
                                           "Account saved",
                                           "Pairing failed"};
      message(o,
              phase == 7,
              status[phase],
              phase == 4   ? "Press and release OK, then press again to authorize."
              : phase == 6 ? "Account stored, not cloud connected. RIGHT: explicit connect."
              : phase == 2 ? "Open the Muse mobile app and pair nearby."
                           : "BACK cancels. No microphone or cloud chat is active.");
      if (phase == 6 || phase == 7) {
        completed = phase == 6;
        break;
      }
    }
    key_edge_t edge;
    while (pop(&edge)) {
      if (!current(o))
        break;
      if (o->operation != HB_MUSE_P4_SETUP || !edge.ticket || edge.ticket != o->view.ticket)
        continue;
      /* A new prompt cannot inherit the consent button or an old
       * release. A preliminary press is ignored locally; its following
       * actual release arms this exact ticket, not any later ticket. */
      if (edge.pressed && !released)
        continue;
      uint8_t packet[17];
      put64(packet, edge.ticket);
      put64(packet + 8, edge.serial);
      packet[16] = edge.pressed;
      if (!rpc(o, RPC_SETUP_EDGE, packet, sizeof packet, &n) || n != 1 || o->response[0] != 1) {
        message(
            o, true, "Pairing confirmation rejected", "No automatic retry of a physical approval.");
        return;
      }
      if (!edge.pressed)
        released = true;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  o->view.confirming = false;
  o->view.ticket = 0;
  if (!completed && !o->view.error)
    message(o, false, "Setup window ended", "No microphone or chat connection. Reopen explicitly.");
  /* CLOSE revokes setup only; a saved account remains in C5 storage. */
  (void)rpc(o, RPC_SETUP_CLOSE, NULL, 0, &n);
}
static void worker(void *arg) {
  owner_t *o = arg;
  bool hold = power_manager_no_sleep_acquire() == ESP_OK;
  if (o->operation == HB_MUSE_P4_CHAT)
    chat(o);
  else
    setup(o);
  mic_stop(o);
  if (o->operation == HB_MUSE_P4_CHAT) {
    bool accepted = control(o, HB_MUSE_APP_REVOKE);
    if (!o->view.error)
      message(o,
              false,
              "Session ended",
              accepted ? "Mic off / RIGHT: new session"
                       : "Mic off / C5 close unconfirmed. Check the C5 connection.");
  } else {
    size_t ignored = 0;
    (void)rpc(o, RPC_SETUP_CLOSE, NULL, 0, &ignored);
  }
  o->view.active = false;
  o->view.ready = false;
  o->view.confirming = false;
  o->view.ticket = 0;
  publish(o);
  if (hold)
    (void)power_manager_no_sleep_release();
  if (o->recording_pcm) {
    memset(o->recording_pcm, 0, HB_MUSE_APP_MAX_FRAMES * sizeof(int16_t));
    heap_caps_free(o->recording_pcm);
  }
  memset(o, 0, sizeof *o);
  heap_caps_free(o);
  portENTER_CRITICAL(&s_lock);
  STORE(s_running, false);
  STORE(s_requested, false);
  STORE(s_held, false);
  s_count = 0;
  s_read = 0;
  memset(s_edges, 0, sizeof s_edges);
  portEXIT_CRITICAL(&s_lock);
  vTaskDelete(NULL);
}
esp_err_t hb_muse_p4_start(hb_muse_p4_operation_t operation) {
  if (operation < HB_MUSE_P4_CHAT || operation > HB_MUSE_P4_TOKEN)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_lock);
  if (s_running) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  STORE(s_running, true);
  STORE(s_requested, true);
  STORE(s_held, true);
  __atomic_add_fetch(&s_release, 1, __ATOMIC_ACQ_REL);
  __atomic_add_fetch(&s_generation, 1, __ATOMIC_ACQ_REL);
  uint32_t generation = s_generation;
  s_count = 0;
  s_read = 0;
  memset(&s_view, 0, sizeof s_view);
  s_view.active = true;
  snprintf(s_view.status, sizeof s_view.status, "Starting");
  portEXIT_CRITICAL(&s_lock);
  owner_t *o = heap_caps_calloc(1, sizeof *o, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!o)
    o = heap_caps_calloc(1, sizeof *o, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (o) {
    o->generation = generation;
    o->operation = operation;
    o->view.active = true;
    if (!s_shutdown && esp_register_shutdown_handler(hb_muse_p4_stop) == ESP_OK)
      s_shutdown = true;
    if (xTaskCreatePinnedToCore(worker, "muse_p4", 8192, o, SYS_PRIO_SERVICE_LO, NULL, 0) == pdPASS)
      return ESP_OK;
    heap_caps_free(o);
  }
  portENTER_CRITICAL(&s_lock);
  STORE(s_running, false);
  STORE(s_requested, false);
  STORE(s_held, false);
  s_view.active = false;
  s_view.error = true;
  snprintf(s_view.status, sizeof s_view.status, "Memory unavailable");
  portEXIT_CRITICAL(&s_lock);
  return ESP_ERR_NO_MEM;
}
