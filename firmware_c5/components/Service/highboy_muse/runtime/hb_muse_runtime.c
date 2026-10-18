#include "hb_muse_runtime.h"
#include "../platform/hb_muse_platform.h"
#include "../platform/hb_muse_account_worker.h"
#include "../platform/hb_muse_store.h"
#include "../transport/hb_muse_endpoint.h"
#include <stdatomic.h>
#include <string.h>
#ifdef HB_MUSE_RUNTIME_HOST_TEST
#include "runtime_host.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "sys_prio.h"
#include "esp_heap_caps.h"
#include "muse_chat.h"
#endif

#define SLOT_COUNT     HB_MUSE_RUNTIME_QUEUE_SLOTS
#define START_LIMIT_MS 10000u
typedef struct {
  uint32_t generation;
  uint16_t bytes;
  uint8_t data[248];
} frame_t;
static QueueHandle_t s_rx_queue, s_tx_queue;
static frame_t s_rx[SLOT_COUNT], s_tx[SLOT_COUNT], s_pending;
static bool s_rx_used[SLOT_COUNT], s_tx_used[SLOT_COUNT], s_pending_valid;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_cache_lock = portMUX_INITIALIZER_UNLOCKED;
static hb_muse_runtime_config_t s_config;
static char s_identity[HB_MUSE_ACCOUNT_ID_CAP];
typedef struct {
  hb_muse_endpoint_t endpoint;
  hb_muse_sdk_snapshot_t sdk;
} owner_heap_t;
static owner_heap_t *s_owner_heap;
#define s_endpoint (s_owner_heap->endpoint)
#define s_sdk      (s_owner_heap->sdk)
static hb_muse_runtime_status_t s_status;
static atomic_bool s_initialized, s_permitted, s_network, s_control_fault;
/* Consent fields and published status below are guarded by s_lock. */
static bool s_wanted_enabled, s_wanted_connect;
static uint32_t s_generation = 1;
/* Remaining state belongs only to the internal-stack owner task. */
static bool s_owner_initialized, s_owner_attempted, s_prepared, s_paired;
static bool s_seen_enabled, s_started, s_stopping, s_fatal, s_clock_started, s_had_epoch;
static uint64_t s_last_ms, s_start_ms, s_connect_ms;
static uint32_t s_seen_generation;
static uint32_t s_requested_revision;
static hb_muse_sdk_session_snapshot_t s_session;
/* Owner-only large state allocated lazily in explicit PSRAM. */

static void wipe(void *memory, size_t size) {
  volatile unsigned char *p = memory;
  while (size--)
    *p++ = 0;
}
static void cache_lock(void *user) {
  (void)user;
  portENTER_CRITICAL(&s_cache_lock);
}
static void cache_unlock(void *user) {
  (void)user;
  portEXIT_CRITICAL(&s_cache_lock);
}
static bool platform_wifi(void *user) {
  (void)user;
  return atomic_load(&s_network);
}
static bool account_current(void *user) {
  (void)user;
  return atomic_load(&s_permitted);
}
static bool platform_settings(void *user, hb_muse_platform_settings_t *out) {
  (void)user;
  bool ok = hb_muse_store_load_settings(NULL, out);
  s_paired = ok && out->account_present; /* called only by the owner refresh */
  return ok;
}
static void set_error(hb_muse_runtime_error_t error, bool fatal) {
  portENTER_CRITICAL(&s_lock);
  s_status.error = error;
  portEXIT_CRITICAL(&s_lock);
  s_fatal |= fatal;
}
static void increment(uint32_t *value) {
  if (*value != UINT32_MAX)
    ++*value;
}
static bool advance_locked(void) {
  if (s_generation == UINT32_MAX)
    return false;
  ++s_generation;
  return true;
}
static bool queue_put(QueueHandle_t queue, frame_t *slots, bool *used, const frame_t *frame) {
  uint8_t index = SLOT_COUNT;
  portENTER_CRITICAL(&s_lock);
  for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
    if (used[i])
      continue;
    used[i] = true;
    slots[i] = *frame;
    index = i;
    break;
  }
  portEXIT_CRITICAL(&s_lock);
  if (index == SLOT_COUNT)
    return false;
  /* Reservation owns this slot until publication; the queue holds no PCM.
   * A concurrent revoke invalidates its generation, even if publish is late. */
  if (xQueueSend(queue, &index, 0) == pdTRUE)
    return true;
  portENTER_CRITICAL(&s_lock);
  wipe(&slots[index], sizeof(slots[index]));
  used[index] = false;
  portEXIT_CRITICAL(&s_lock);
  return false;
}
static bool queue_get(QueueHandle_t queue, frame_t *slots, bool *used, frame_t *frame) {
  uint8_t index;
  if (xQueueReceive(queue, &index, 0) != pdTRUE)
    return false;
  portENTER_CRITICAL(&s_lock);
  bool valid = index < SLOT_COUNT && used[index];
  if (valid) {
    *frame = slots[index];
    wipe(&slots[index], sizeof(slots[index]));
    used[index] = false;
  }
  portEXIT_CRITICAL(&s_lock);
  return valid;
}
static void drop_queued(void) {
  frame_t frame;
  while (queue_get(s_rx_queue, s_rx, s_rx_used, &frame))
    wipe(&frame, sizeof(frame));
  while (queue_get(s_tx_queue, s_tx, s_tx_used, &frame))
    wipe(&frame, sizeof(frame));
  wipe(&s_pending, sizeof(s_pending));
  s_pending_valid = false;
}
static bool endpoint_quiet(void) {
  return s_endpoint.binding.adapter.phase == HB_MUSE_ADAPTER_IDLE && !s_endpoint.binding.owns_sdk;
}
static bool fenced(void) {
  return !s_started || (s_session.fenced && !s_session.enabled && !s_session.requested_enabled &&
                        s_session.requested_revision == s_requested_revision &&
                        s_session.requested_revision == s_session.applied_revision);
}
static bool session_access(bool enabled) {
  uint32_t revision = hb_muse_sdk_session_access(enabled);
  if (!revision)
    return false;
  s_requested_revision = revision;
  /* Never publish the previous revision's fence/readiness after dispatch. */
  s_session.requested_revision = revision;
  s_session.requested_enabled = enabled;
  s_session.enabled = false;
  s_session.fenced = false;
  return hb_muse_sdk_session_snapshot(&s_session);
}
static void stop_owner(void) {
  atomic_store(&s_permitted, false);
  hb_muse_platform_revoke();
  hb_muse_account_worker_revoke();
  hb_muse_endpoint_access(&s_endpoint, false, s_paired, atomic_load(&s_network));
  s_prepared = false;
  s_seen_enabled = false;
  s_stopping = true;
  portENTER_CRITICAL(&s_lock);
  s_wanted_connect = false;
  if (!advance_locked())
    s_fatal = true;
  s_seen_generation = s_generation;
  portEXIT_CRITICAL(&s_lock);
  drop_queued();
  if (s_started && !session_access(false))
    set_error(HB_MUSE_RUNTIME_CONTROL_ERROR, true);
}
static bool initialize_owner(void) {
  if (s_owner_attempted)
    return s_owner_initialized;
  s_owner_attempted = true;
  const hb_muse_platform_ops_t platform = {
      cache_lock, cache_unlock, platform_wifi, platform_settings, hb_muse_account_worker_lookup};
  /* No settings read or SDK start here. These init functions are inert. */
  s_owner_initialized = hb_muse_store_init(s_identity, account_current, NULL) &&
                        hb_muse_account_worker_init(hb_muse_store_account_ops(), NULL) &&
                        hb_muse_platform_init(&platform, NULL) &&
                        hb_muse_endpoint_init(&s_endpoint, s_config.boot_id);
  if (!s_owner_initialized)
    set_error(HB_MUSE_RUNTIME_INIT_ERROR, true);
  /* Initial disabled setup is not a revoke and must not discard ingress. */
  if (s_owner_initialized)
    s_seen_generation = 1;
  return s_owner_initialized;
}
static void publish(void) {
  portENTER_CRITICAL(&s_lock);
  s_status.enabled = s_wanted_enabled;
  s_status.paired = s_paired;
  s_status.network = atomic_load(&s_network);
  s_status.cloud_authorized = atomic_load(&s_permitted);
  s_status.worker_started = s_sdk.worker_started;
  s_status.session_ready = s_sdk.session_ready && s_status.cloud_authorized && s_session.enabled;
  s_status.fenced = s_seen_generation == s_generation && fenced() && endpoint_quiet() &&
                    hb_muse_account_worker_idle();
  s_status.session_requested_revision = s_session.requested_revision;
  s_status.session_applied_revision = s_session.applied_revision;
  if (s_fatal)
    s_status.phase = HB_MUSE_RUNTIME_FAULT;
  else if (s_stopping || s_seen_generation != s_generation)
    s_status.phase = HB_MUSE_RUNTIME_QUIESCING;
  else if (!s_wanted_enabled)
    s_status.phase = HB_MUSE_RUNTIME_DISABLED;
  else if (!s_prepared || !s_paired)
    s_status.phase = HB_MUSE_RUNTIME_UNPAIRED;
  else if (!s_status.network)
    s_status.phase = HB_MUSE_RUNTIME_OFFLINE;
  else if (s_wanted_connect && !s_sdk.worker_started)
    s_status.phase = HB_MUSE_RUNTIME_STARTING;
  else if (s_wanted_connect)
    s_status.phase = s_status.session_ready ? HB_MUSE_RUNTIME_ONLINE : HB_MUSE_RUNTIME_CONNECTING;
  else
    s_status.phase = HB_MUSE_RUNTIME_PREPARED;
  portEXIT_CRITICAL(&s_lock);
}
static void owner_step(void) {
  if (!initialize_owner()) {
    publish();
    return;
  }
  if (atomic_load(&s_control_fault))
    set_error(HB_MUSE_RUNTIME_CONTROL_ERROR, true);
  uint64_t now = s_config.now_ms(s_config.user);
  if (s_clock_started && now < s_last_ms) {
    set_error(HB_MUSE_RUNTIME_CLOCK_ERROR, true);
    stop_owner();
  }
  s_clock_started = true;
  if (now >= s_last_ms)
    s_last_ms = now;
  bool network = s_config.wifi_connected(s_config.user);
  bool was_network = atomic_exchange(&s_network, network);
  bool enabled, connect;
  uint32_t generation;
  portENTER_CRITICAL(&s_lock);
  enabled = s_wanted_enabled;
  connect = s_wanted_connect;
  generation = s_generation;
  portEXIT_CRITICAL(&s_lock);
  bool revoked = generation != s_seen_generation;
  s_seen_generation = generation;
  if (revoked || (was_network && !network && (connect || atomic_load(&s_permitted)))) {
    stop_owner();
    connect = false;
  }
  if (s_fatal && !s_stopping) {
    stop_owner();
    connect = false;
  }
  if (s_started) {
    if (!hb_muse_sdk_session_snapshot(&s_session) || !hb_muse_sdk_snapshot(&s_sdk)) {
      set_error(HB_MUSE_RUNTIME_CONTROL_ERROR, true);
      stop_owner();
      connect = false;
    }
  }
  hb_muse_endpoint_access(&s_endpoint, atomic_load(&s_permitted), s_paired, network);
  hb_muse_endpoint_pump(&s_endpoint, s_last_ms);
  if (s_had_epoch && !s_endpoint.server.active && atomic_load(&s_permitted)) {
    set_error(HB_MUSE_RUNTIME_LINK_EXPIRED, false);
    stop_owner();
    connect = false;
  }
  s_had_epoch = s_endpoint.server.active;
  if (s_stopping && fenced() && endpoint_quiet() && hb_muse_account_worker_idle())
    s_stopping = false;
  if (!enabled)
    s_seen_enabled = false;
  if (enabled && !s_fatal && !s_stopping && !s_seen_enabled) {
    /* Only the internal-stack owner reads the existing NVS provider. */
    s_prepared = hb_muse_platform_refresh();
    s_seen_enabled = true;
    if (!s_prepared)
      set_error(HB_MUSE_RUNTIME_STORE_ERROR, false);
  }
  if (connect && enabled && s_prepared && s_paired && network && !s_fatal && !s_stopping) {
    if (!s_started) {
      s_started = true;
      s_start_ms = s_connect_ms = now;
      /* Platform/account remain disabled while the SDK starts. */
      muse_hatch_start();
      if (!session_access(false)) {
        set_error(HB_MUSE_RUNTIME_CONTROL_ERROR, true);
        stop_owner();
      }
    } else if (!s_sdk.worker_started && now - s_start_ms >= START_LIMIT_MS) {
      set_error(HB_MUSE_RUNTIME_START_ERROR, true);
      stop_owner();
    } else if (!atomic_load(&s_permitted) && s_sdk.worker_started && fenced() && endpoint_quiet() &&
               hb_muse_account_worker_idle()) {
      hb_muse_account_worker_access(true, true, true);
      hb_muse_platform_set_enabled(true);
      portENTER_CRITICAL(&s_lock);
      bool current = generation == s_generation && s_wanted_enabled && s_wanted_connect;
      atomic_store(&s_permitted, current);
      portEXIT_CRITICAL(&s_lock);
      if (!current) {
        stop_owner();
      } else if (!session_access(true)) {
        set_error(HB_MUSE_RUNTIME_CONTROL_ERROR, true);
        stop_owner();
      } else if (!atomic_load(&s_permitted)) {
        stop_owner();
      } else
        s_connect_ms = now;
    }
    if (atomic_load(&s_permitted) && now - s_connect_ms >= HB_MUSE_RUNTIME_SESSION_MS) {
      set_error(HB_MUSE_RUNTIME_SESSION_EXPIRED, false);
      stop_owner();
    }
  }
  /* Backpressure cannot stop lease expiry/cancellation above. Never dispatch
   * a later request while an earlier serialized reply lacks a queue slot. */
  if (s_pending_valid && queue_put(s_tx_queue, s_tx, s_tx_used, &s_pending)) {
    wipe(&s_pending, sizeof(s_pending));
    s_pending_valid = false;
  }
  if (!s_pending_valid && !s_fatal) {
    frame_t request;
    if (queue_get(s_rx_queue, s_rx, s_rx_used, &request)) {
      portENTER_CRITICAL(&s_lock);
      generation = s_generation;
      portEXIT_CRITICAL(&s_lock);
      size_t bytes = 0;
      if (request.generation == generation && hb_muse_endpoint_process(&s_endpoint,
                                                                       request.data,
                                                                       request.bytes,
                                                                       s_last_ms,
                                                                       s_pending.data,
                                                                       sizeof(s_pending.data),
                                                                       &bytes)) {
        s_pending.bytes = (uint16_t)bytes;
        s_pending.generation = generation;
        s_pending_valid = true;
        portENTER_CRITICAL(&s_lock);
        increment(&s_status.processed_frames);
        portEXIT_CRITICAL(&s_lock);
      }
      wipe(&request, sizeof(request));
    }
  }
  publish();
}
static void owner_task(void *user) {
  (void)user;
  for (;;) {
    owner_step();
    vTaskDelay(pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1);
  }
}
bool hb_muse_runtime_init(const hb_muse_runtime_config_t *config) {
  if (atomic_load(&s_initialized) || !config || !config->boot_id || !config->existing_device_id ||
      !config->wifi_connected || !config->now_ms)
    return false;
  size_t size = 0;
  while (size < sizeof(s_identity) && config->existing_device_id[size])
    ++size;
  if (!size || size == sizeof(s_identity))
    return false;
  // Stable address for endpoint callback contexts; no internal-memory fallback.
  s_owner_heap = heap_caps_calloc(1, sizeof(*s_owner_heap), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_owner_heap)
    return false;
  s_config = *config;
  memcpy(s_identity, config->existing_device_id, size + 1);
  s_config.existing_device_id = s_identity;
  s_rx_queue =
      xQueueCreateWithCaps(SLOT_COUNT, sizeof(uint8_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_tx_queue =
      xQueueCreateWithCaps(SLOT_COUNT, sizeof(uint8_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_rx_queue || !s_tx_queue)
    goto failed;
  s_status.phase = HB_MUSE_RUNTIME_INITIALIZING;
  atomic_store(&s_initialized, true);
  if (xTaskCreatePinnedToCoreWithCaps(owner_task,
                                      "muse_owner",
                                      8192,
                                      NULL,
                                      SYS_PRIO_SERVICE_LO,
                                      NULL,
                                      SYS_CORE_MAIN,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) == pdPASS)
    return true;
  atomic_store(&s_initialized, false);
failed:
  if (s_rx_queue)
    vQueueDeleteWithCaps(s_rx_queue);
  if (s_tx_queue)
    vQueueDeleteWithCaps(s_tx_queue);
  s_rx_queue = s_tx_queue = NULL;
  wipe(s_identity, sizeof(s_identity));
  // No worker was created on this path; successful lifetime is until reboot.
  wipe(s_owner_heap, sizeof(*s_owner_heap));
  heap_caps_free(s_owner_heap);
  s_owner_heap = NULL;
  return false;
}
bool hb_muse_runtime_set_enabled(bool enabled) {
  if (!atomic_load(&s_initialized))
    return false;
  portENTER_CRITICAL(&s_lock);
  bool ok = s_status.phase != HB_MUSE_RUNTIME_FAULT;
  if (s_wanted_enabled != enabled) {
    ok = ok && advance_locked();
    s_wanted_enabled = enabled && ok;
    if (!ok) {
      atomic_store(&s_control_fault, true);
      atomic_store(&s_permitted, false);
    }
    if (!enabled) {
      s_wanted_connect = false;
      atomic_store(&s_permitted, false);
    }
    s_status.fenced = s_status.session_ready = false;
  }
  portEXIT_CRITICAL(&s_lock);
  return ok;
}
bool hb_muse_runtime_connect(void) {
  if (!atomic_load(&s_initialized))
    return false;
  portENTER_CRITICAL(&s_lock);
  bool ok = s_wanted_enabled && s_status.phase == HB_MUSE_RUNTIME_PREPARED && s_status.paired &&
            s_status.network && s_status.fenced;
  if (ok) {
    s_wanted_connect = true;
    s_status.fenced = s_status.session_ready = false;
  }
  portEXIT_CRITICAL(&s_lock);
  return ok;
}
void hb_muse_runtime_revoke(void) {
  if (!atomic_load(&s_initialized))
    return;
  atomic_store(&s_permitted, false);
  portENTER_CRITICAL(&s_lock);
  /* Serialize with the owner's permission grant: an intervening owner
   * cannot resurrect access after this revoke returns. */
  atomic_store(&s_permitted, false);
  s_wanted_enabled = s_wanted_connect = false;
  s_status.fenced = s_status.session_ready = false;
  if (!advance_locked()) {
    s_status.phase = HB_MUSE_RUNTIME_FAULT;
    atomic_store(&s_control_fault, true);
  }
  portEXIT_CRITICAL(&s_lock);
}
bool hb_muse_runtime_submit(const uint8_t *bytes, size_t size) {
  if (!atomic_load(&s_initialized) || !bytes || !size || size > HB_MUSE_RUNTIME_FRAME_MAX)
    return false;
  frame_t frame = {0};
  frame.bytes = (uint16_t)size;
  memcpy(frame.data, bytes, size);
  portENTER_CRITICAL(&s_lock);
  frame.generation = s_generation;
  portEXIT_CRITICAL(&s_lock);
  bool accepted = queue_put(s_rx_queue, s_rx, s_rx_used, &frame);
  portENTER_CRITICAL(&s_lock);
  increment(accepted ? &s_status.accepted_frames : &s_status.rejected_frames);
  portEXIT_CRITICAL(&s_lock);
  wipe(&frame, sizeof(frame));
  return accepted;
}
bool hb_muse_runtime_take_reply(uint8_t *bytes, size_t capacity, size_t *size) {
  if (size)
    *size = 0;
  if (!atomic_load(&s_initialized) || !bytes || !size || capacity < HB_MUSE_RUNTIME_FRAME_MAX)
    return false;
  frame_t frame;
  while (queue_get(s_tx_queue, s_tx, s_tx_used, &frame)) {
    portENTER_CRITICAL(&s_lock);
    bool current = frame.generation == s_generation;
    if (current) {
      memcpy(bytes, frame.data, frame.bytes);
      *size = frame.bytes;
    }
    portEXIT_CRITICAL(&s_lock);
    wipe(&frame, sizeof(frame));
    if (current)
      return true;
  }
  return false;
}
bool hb_muse_runtime_status(hb_muse_runtime_status_t *out) {
  if (!atomic_load(&s_initialized) || !out)
    return false;
  portENTER_CRITICAL(&s_lock);
  *out = s_status;
  out->enabled = s_wanted_enabled;
  out->cloud_authorized = atomic_load(&s_permitted);
  if (!out->cloud_authorized)
    out->session_ready = false;
  portEXIT_CRITICAL(&s_lock);
  return true;
}
#ifdef HB_MUSE_RUNTIME_HOST_TEST
void hb_muse_runtime_host_step(void) {
  if (atomic_load(&s_initialized))
    owner_step();
}
#endif
