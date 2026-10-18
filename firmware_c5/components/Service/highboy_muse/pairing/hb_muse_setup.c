#include "hb_muse_setup.h"
#include "hb_muse_pairing.h"
#include "hb_muse_pairing_gatt.h"
#include "hb_muse_pairing_fragments.h"
#include "../runtime/hb_muse_runtime.h"
#include "../platform/hb_muse_store.h"
#include "link_pairing.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#ifdef HB_MUSE_SETUP_HOST_TEST
#include "setup_host.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "sys_prio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#endif

/* Fixed stage names and numeric state only. Never log credentials or RX data.
 * Called by the setup owner outside locks, not by the SPI status poll. */
static void setup_trace(const char *stage, unsigned error) {
#ifdef HB_MUSE_SETUP_HOST_TEST
  (void)stage;
  (void)error;
#else
  hb_muse_runtime_status_t r = {0};
  bool valid = hb_muse_runtime_status(&r);
  ESP_LOGI("MUSE_SETUP",
           "stage=%s error=%u runtime=%d/%d valid=%d fenced=%d internal=%u largest=%u",
           stage,
           error,
           (int)r.phase,
           (int)r.error,
           (int)valid,
           (int)r.fenced,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
}

typedef struct {
  uint64_t window, connection, ticket, edge, at_ms;
  uint16_t bytes;
  bool button, pressed;
  uint8_t data[HB_MUSE_PAIRING_ATT_MAX];
} event_t;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static hb_muse_setup_config_t s_ops;
static hb_muse_setup_status_t s_status;
static char s_node[128], s_device[128], s_mac[18], s_version[64];
static char s_token[HB_MUSE_SETUP_TOKEN_CAP];
static event_t s_events[HB_MUSE_SETUP_RX_SLOTS];
static uint8_t s_head, s_count;
static atomic_bool s_initialized;
/* Fields below are protected by s_lock, including asynchronous GAP facts. */
static bool s_wanted, s_active, s_link, s_notify, s_clock_fault;
static uint64_t s_window, s_connection, s_deadline, s_requested_ms, s_latest_ms;
static uint16_t s_handle, s_mtu;
/* Exactly one internal-stack owner below; never touched by callbacks. */
static bool s_pair_initialized, s_ble_owned, s_pair_attempted;
static uint64_t s_owner_window, s_owner_connection;
static uint64_t s_saved_at;
static uint64_t s_diagnostic_window;
static hb_muse_pairing_fragments_t s_fragments;
static void wipe(void *memory, size_t n) {
  volatile unsigned char *p = memory;
  while (n--)
    *p++ = 0;
}
static bool copy(char *out, size_t cap, const char *text) {
  if (!text)
    return false;
  size_t n = 0;
  while (n < cap && text[n])
    ++n;
  if (!n || n == cap)
    return false;
  for (size_t i = 0; i < n; ++i)
    if ((unsigned char)text[i] < 0x21 || (unsigned char)text[i] > 0x7e)
      return false;
  memcpy(out, text, n + 1u);
  return true;
}
static void clear_events_locked(void) {
  wipe(s_events, sizeof(s_events));
  s_head = s_count = 0;
}
static bool time_locked(uint64_t now) {
  if (s_clock_fault || now < s_latest_ms) {
    s_clock_fault = true;
    s_wanted = s_active = false;
    s_status.error = HB_MUSE_SETUP_CLOCK;
    return false;
  }
  s_latest_ms = now;
  return true;
}
static void invalidate_locked(hb_muse_setup_error_t error) {
  s_wanted = s_active = s_notify = false;
  s_status.error = error;
  s_status.confirmation_ticket = 0;
  s_status.release_seen = false;
  s_status.phase = HB_MUSE_SETUP_QUIESCING;
}
static bool live(uint64_t window, uint64_t connection) {
  uint64_t now = s_ops.now_ms(s_ops.user);
  portENTER_CRITICAL(&s_lock);
  /* Async callers can be preempted between clock sample and lock. Only the
   * serialized owner compares successive samples; do not falsely interpret
   * a delayed caller's older sample as a global clock rollback. */
  bool ok = !s_clock_fault && s_wanted && s_active && s_link && s_notify && s_window == window &&
            s_connection == connection && now >= s_requested_ms && now < s_deadline;
  portEXIT_CRITICAL(&s_lock);
  return ok;
}
static uint64_t pairing_now(void *unused) {
  (void)unused;
  return s_ops.now_ms(s_ops.user);
}
static bool pairing_current(void *unused, uint64_t window, uint64_t connection) {
  (void)unused;
  return live(window, connection) && s_ops.services_quiet(s_ops.user);
}
static bool runtime_quiet(void) {
  hb_muse_runtime_status_t status;
  return hb_muse_runtime_status(&status) && status.fenced && !status.enabled &&
         !status.cloud_authorized && status.phase != HB_MUSE_RUNTIME_FAULT &&
         status.phase != HB_MUSE_RUNTIME_INITIALIZING;
}
static bool transport_current(void *unused, uint16_t handle) {
  (void)unused;
  portENTER_CRITICAL(&s_lock);
  uint64_t window = s_window, connection = s_connection;
  bool matched = handle == s_handle;
  portEXIT_CRITICAL(&s_lock);
  return matched && live(window, connection);
}
static bool enqueue(const event_t *event) {
  bool ok = false;
  portENTER_CRITICAL(&s_lock);
  if (s_wanted && s_active && s_link && s_notify && event->window == s_window &&
      event->connection == s_connection) {
    if (s_count < HB_MUSE_SETUP_RX_SLOTS) {
      uint8_t index = (uint8_t)((s_head + s_count) % HB_MUSE_SETUP_RX_SLOTS);
      s_events[index] = *event;
      ++s_count;
      ok = true;
    } else {
      if (s_status.dropped != UINT32_MAX)
        ++s_status.dropped;
      invalidate_locked(HB_MUSE_SETUP_OVERFLOW);
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return ok;
}
static bool defer_rx(void *unused, uint16_t handle, const uint8_t *data, size_t bytes) {
  (void)unused;
  if (!data || !bytes || bytes > HB_MUSE_PAIRING_ATT_MAX || !transport_current(NULL, handle))
    return false;
  event_t event = {0};
  portENTER_CRITICAL(&s_lock);
  event.window = s_window;
  event.connection = s_connection;
  portEXIT_CRITICAL(&s_lock);
  event.at_ms = s_ops.now_ms(s_ops.user);
  event.bytes = (uint16_t)bytes;
  memcpy(event.data, data, bytes);
  bool ok = enqueue(&event);
  wipe(&event, sizeof(event));
  return ok;
}
static bool register_service(void) {
  const hb_muse_pairing_gatt_config_t config = {transport_current, defer_rx, NULL};
  return hb_muse_pairing_gatt_register(&config);
}
static void owner_delay(unsigned milliseconds) {
#ifdef HB_MUSE_SETUP_HOST_TEST
  hb_setup_test_delay(milliseconds);
#else
  vTaskDelay(pdMS_TO_TICKS(milliseconds));
#endif
}
static bool
send_json(void *unused, uint64_t connection, const char *json, size_t bytes, uint32_t record) {
  (void)unused;
  uint8_t fragment[HB_MUSE_PAIRING_NOTIFY_MAX];
  uint8_t index = 0, total = 0;
  do {
    portENTER_CRITICAL(&s_lock);
    uint64_t window = s_window;
    uint16_t handle = s_handle, mtu = s_mtu;
    portEXIT_CRITICAL(&s_lock);
    size_t size = 0;
    bool ok = live(window, connection) &&
              (!record || link_pairing_record_session_is_current(record)) &&
              hb_muse_pairing_fragment_encode(
                  (const uint8_t *)json, bytes, mtu, index, fragment, &size, &total) &&
              hb_muse_pairing_gatt_notify(handle, fragment, size);
    wipe(fragment, sizeof(fragment));
    if (!ok)
      return false;
    /* Match the SDK pacing; accepted notifications are not retransmitted.
     * No setup critical section is held while the host drains its queue. */
    owner_delay(50);
    if (!live(window, connection) || (record && !link_pairing_record_session_is_current(record)))
      return false;
    ++index;
  } while (index < total);
  return true;
}
typedef struct {
  bool (*guard)(void *);
  void *user;
} commit_guard_t;
static bool commit_current(void *user) {
  commit_guard_t *guard = user;
  return guard->guard(guard->user) && runtime_quiet() && s_ops.services_quiet(s_ops.user);
}
static bool store(void *unused,
                  const hb_muse_pairing_credentials_t *credentials,
                  bool (*guard)(void *),
                  void *guard_user) {
  (void)unused;
  commit_guard_t proof = {guard, guard_user};
  if (!commit_current(&proof))
    return false;
  /* Do not use stack for this 4+KiB credential record. */
  hb_muse_store_record_t *record = calloc(1, sizeof(*record));
  if (!record)
    return false;
  record->magic = HB_MUSE_STORE_MAGIC;
  record->version = 1;
  bool ok = copy(record->device_id, sizeof(record->device_id), s_node) &&
            copy(record->access, sizeof(record->access), credentials->access_token) &&
            copy(record->refresh, sizeof(record->refresh), credentials->refresh_token);
  if (ok) {
    ok = hb_muse_store_import_initial(record, commit_current, &proof) && commit_current(&proof);
    portENTER_CRITICAL(&s_lock);
    s_status.account_stored = ok;
    if (!ok)
      s_status.error = HB_MUSE_SETUP_STORE;
    portEXIT_CRITICAL(&s_lock);
  }
  wipe(record, sizeof(*record));
  free(record);
  return ok;
}
static bool connect_wifi(void *unused,
                         const hb_muse_pairing_credentials_t *credentials,
                         bool (*guard)(void *),
                         void *guard_user) {
  (void)unused;
  commit_guard_t proof = {guard, guard_user};
  bool ok = commit_current(&proof) &&
            s_ops.wifi_provision(
                s_ops.user, credentials->ssid, credentials->password, commit_current, &proof) &&
            commit_current(&proof);
  portENTER_CRITICAL(&s_lock);
  s_status.wifi_requested = ok;
  if (!ok)
    s_status.error = HB_MUSE_SETUP_WIFI;
  portEXIT_CRITICAL(&s_lock);
  return ok;
}
static bool scan_wifi(void *unused,
                      hb_muse_pairing_network_t *out,
                      size_t capacity,
                      size_t *count,
                      bool (*guard)(void *),
                      void *guard_user) {
  (void)unused;
  commit_guard_t proof = {guard, guard_user};
  if (count)
    *count = 0;
  return out && count && s_ops.wifi_scan && commit_current(&proof) &&
         s_ops.wifi_scan(s_ops.user, out, capacity, count, commit_current, &proof) &&
         commit_current(&proof);
}
static void teardown(void) {
  if (s_pair_initialized)
    hb_muse_pairing_revoke();
  hb_muse_pairing_fragments_drop(&s_fragments);
  portENTER_CRITICAL(&s_lock);
  s_active = s_notify = s_link = false;
  clear_events_locked();
  portEXIT_CRITICAL(&s_lock);
  if (s_ble_owned) {
    s_ops.release_ble(s_ops.user);
    s_ble_owned = false;
  }
  s_owner_window = s_owner_connection = s_saved_at = 0;
  portENTER_CRITICAL(&s_lock);
  s_status.busy = false;
  s_status.subscribed = false;
  s_status.confirmation_ticket = 0;
  s_status.release_seen = false;
  s_status.phase = s_status.error == HB_MUSE_SETUP_OK ? HB_MUSE_SETUP_OFF : HB_MUSE_SETUP_FAILED;
  portEXIT_CRITICAL(&s_lock);
}
static void reject(hb_muse_setup_error_t error) {
  setup_trace("rejected", (unsigned)error);
  portENTER_CRITICAL(&s_lock);
  if (s_status.error == HB_MUSE_SETUP_OK)
    s_status.error = error;
  invalidate_locked(s_status.error);
  portEXIT_CRITICAL(&s_lock);
  teardown();
}
static bool pairing_initialize(void) {
  if (s_pair_attempted)
    return s_pair_initialized;
  s_pair_attempted = true;
  hb_muse_pairing_config_t config = {pairing_now,
                                     pairing_current,
                                     send_json,
                                     store,
                                     NULL,
                                     s_node,
                                     s_device,
                                     s_mac,
                                     s_version,
                                     s_token,
                                     scan_wifi,
                                     connect_wifi};
  s_pair_initialized = hb_muse_pairing_init(&config);
  /* Pairing singleton retains its private copy for later local windows. */
  wipe(s_token, sizeof(s_token));
  return s_pair_initialized;
}
static void owner_step(void) {
  if (!atomic_load(&s_initialized))
    return;
  uint64_t now = s_ops.now_ms(s_ops.user);
  portENTER_CRITICAL(&s_lock);
  bool clock_ok = time_locked(now);
  if (s_wanted && now >= s_deadline)
    invalidate_locked(HB_MUSE_SETUP_TIMEOUT);
  bool wanted = s_wanted, busy = s_status.busy, linked = s_link;
  uint64_t window = s_window, connection = s_connection, requested = s_requested_ms;
  portEXIT_CRITICAL(&s_lock);
  if (!clock_ok || !wanted) {
    if (busy || s_ble_owned)
      teardown();
    return;
  }
  if (s_diagnostic_window != window) {
    s_diagnostic_window = window;
    setup_trace("window_open", 0);
  }
  if (!s_owner_window) {
    if (!runtime_quiet()) {
      if (now - requested >= 10000u)
        reject(HB_MUSE_SETUP_RUNTIME);
      return;
    }
    hb_muse_platform_settings_t settings;
    setup_trace("read_account", 0);
    if (!hb_muse_store_load_settings(NULL, &settings)) {
      reject(HB_MUSE_SETUP_STORE);
      return;
    }
    if (settings.account_present) {
      reject(HB_MUSE_SETUP_ALREADY_PAIRED);
      return;
    }
    setup_trace("pairing_initialize", 0);
    if (!pairing_initialize()) {
      reject(HB_MUSE_SETUP_PROTOCOL);
      return;
    }
    setup_trace("acquire_bluetooth", 0);
    if (!s_ops.acquire_ble(s_ops.user, register_service)) {
      reject(HB_MUSE_SETUP_BT);
      return;
    }
    s_ble_owned = true;
    portENTER_CRITICAL(&s_lock);
    wanted = s_wanted && s_window == window;
    portEXIT_CRITICAL(&s_lock);
    if (!wanted || !runtime_quiet() || !s_ops.services_quiet(s_ops.user)) {
      reject(HB_MUSE_SETUP_RUNTIME);
      return;
    }
    setup_trace("pairing_open", 0);
    if (!hb_muse_pairing_open(window, now)) {
      reject(HB_MUSE_SETUP_PROTOCOL);
      return;
    }
    s_owner_window = window;
    portENTER_CRITICAL(&s_lock);
    s_active = true;
    s_status.phase = HB_MUSE_SETUP_ADVERTISING;
    portEXIT_CRITICAL(&s_lock);
    setup_trace("advertise", 0);
    if (!s_ops.advertise(s_ops.user)) {
      reject(HB_MUSE_SETUP_BT);
      return;
    }
    setup_trace("advertising_ready", 0);
  }
  if (!runtime_quiet() || !s_ops.services_quiet(s_ops.user)) {
    reject(HB_MUSE_SETUP_RUNTIME);
    return;
  }
  if (s_saved_at) {
    /* A phone may disconnect after receiving auth_ok. That must neither
     * discard a verified saved outcome nor re-advertise the paired board. */
    portENTER_CRITICAL(&s_lock);
    if (s_wanted && s_active && now >= s_saved_at && now - s_saved_at >= 2000u)
      s_status.phase = HB_MUSE_SETUP_SAVED;
    portEXIT_CRITICAL(&s_lock);
    return;
  }
  portENTER_CRITICAL(&s_lock);
  linked = s_link;
  connection = s_connection;
  wanted = s_wanted;
  bool subscribed = s_notify;
  portEXIT_CRITICAL(&s_lock);
  if (!wanted) {
    teardown();
    return;
  }
  if (s_owner_connection && (!linked || connection != s_owner_connection)) {
    hb_muse_pairing_disconnect(s_owner_connection);
    s_owner_connection = 0;
    hb_muse_pairing_fragments_drop(&s_fragments);
    if (!linked && hb_muse_pairing_status().phase == HB_MUSE_PAIRING_OPEN &&
        !s_ops.advertise(s_ops.user)) {
      reject(HB_MUSE_SETUP_BT);
      return;
    }
  }
  /* A real phone normally subscribes AFTER GAP CONNECT. Keep the pairing
   * core unconnected until CCCD is ready; its current() guard correctly
   * requires notifications and would reject a premature core connection. */
  if (linked && subscribed && !s_owner_connection) {
    if (!hb_muse_pairing_connected(connection, now)) {
      reject(HB_MUSE_SETUP_PROTOCOL);
      return;
    }
    s_owner_connection = connection;
  }
  /* At most 8 fragments per pass; never crypto/parser/NVS on SPI/GATT. */
  for (unsigned i = 0; i < HB_MUSE_SETUP_RX_SLOTS; ++i) {
    event_t event = {0};
    portENTER_CRITICAL(&s_lock);
    bool found = s_count != 0;
    if (found) {
      event = s_events[s_head];
      wipe(&s_events[s_head], sizeof(event));
      s_head = (uint8_t)((s_head + 1u) % HB_MUSE_SETUP_RX_SLOTS);
      --s_count;
    }
    portEXIT_CRITICAL(&s_lock);
    if (!found)
      break;
    bool current = live(event.window, event.connection);
    now = s_ops.now_ms(s_ops.user);
    if (current && (now < event.at_ms || now - event.at_ms >= HB_MUSE_PAIRING_FRAGMENT_TTL_MS)) {
      wipe(&event, sizeof(event));
      reject(HB_MUSE_SETUP_TIMEOUT);
      return;
    }
    if (current && event.button) {
      (void)hb_muse_pairing_button(event.ticket, event.edge, event.pressed, now);
    } else if (current) {
      uint8_t *json = NULL;
      size_t bytes = 0;
      hb_muse_fragment_result_t result = hb_muse_pairing_fragments_feed(
          &s_fragments, event.connection, event.data, event.bytes, now, &json, &bytes);
      bool ok = result != HB_MUSE_FRAGMENT_BAD;
      if (result == HB_MUSE_FRAGMENT_READY)
        ok = hb_muse_pairing_receive(event.connection, json, bytes, now);
      hb_muse_pairing_fragments_release(json, bytes);
      if (!ok) {
        wipe(&event, sizeof(event));
        reject(HB_MUSE_SETUP_PROTOCOL);
        return;
      }
    }
    wipe(&event, sizeof(event));
  }
  now = s_ops.now_ms(s_ops.user);
  hb_muse_pairing_fragments_expire(&s_fragments, now);
  hb_muse_pairing_poll(now);
  hb_muse_pairing_status_t pair = hb_muse_pairing_status();
  if (pair.phase == HB_MUSE_PAIRING_FAILED) {
    reject(HB_MUSE_SETUP_PROTOCOL);
    return;
  }
  /* Keep BLE alive after the final auth_ok notification, matching the
   * official SDK's 2s drain grace without restarting or connecting cloud. */
  if (pair.phase == HB_MUSE_PAIRING_STORED && !s_saved_at)
    s_saved_at = now;
  bool saved =
      pair.phase == HB_MUSE_PAIRING_STORED && now >= s_saved_at && now - s_saved_at >= 2000u;
  portENTER_CRITICAL(&s_lock);
  if (s_wanted && s_active && s_window == s_owner_window) {
    s_status.subscribed = s_notify;
    s_status.confirmation_ticket = pair.confirmation_ticket;
    s_status.release_seen = pair.release_seen;
    s_status.phase =
        saved ? HB_MUSE_SETUP_SAVED
        : pair.phase == HB_MUSE_PAIRING_CONFIRMED || pair.phase == HB_MUSE_PAIRING_STORED
            ? HB_MUSE_SETUP_CONFIRMED
        : pair.phase == HB_MUSE_PAIRING_CONFIRM ? HB_MUSE_SETUP_CONFIRM
        : s_link                                ? HB_MUSE_SETUP_PHONE
                                                : HB_MUSE_SETUP_ADVERTISING;
  }
  portEXIT_CRITICAL(&s_lock);
}
#ifndef HB_MUSE_SETUP_HOST_TEST
static void task(void *unused) {
  (void)unused;
  for (;;) {
    owner_step();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
#else
void hb_muse_setup_host_step(void) {
  owner_step();
}
#endif
bool hb_muse_setup_init(const hb_muse_setup_config_t *config) {
  if (atomic_load(&s_initialized) || !config || !config->now_ms || !config->acquire_ble ||
      !config->advertise || !config->release_ble || !config->services_quiet ||
      !config->wifi_provision || !copy(s_node, sizeof(s_node), config->node_id) ||
      !copy(s_device, sizeof(s_device), config->device_id) ||
      !copy(s_mac, sizeof(s_mac), config->mac) ||
      !copy(s_version, sizeof(s_version), config->firmware_version))
    return false;
  s_ops = *config;
  s_status.phase = HB_MUSE_SETUP_OFF;
#ifndef HB_MUSE_SETUP_HOST_TEST
  if (xTaskCreatePinnedToCoreWithCaps(task,
                                      "muse_setup",
                                      8192,
                                      NULL,
                                      SYS_PRIO_SERVICE_LO,
                                      NULL,
                                      SYS_CORE_MAIN,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS)
    return false;
#endif
  atomic_store(&s_initialized, true);
  return true;
}
bool hb_muse_setup_set_local_token_guarded(const char *token,
                                           size_t bytes,
                                           bool (*guard)(void *),
                                           void *guard_user) {
  if (!atomic_load(&s_initialized) || !token || bytes <= 5u || bytes >= sizeof(s_token) ||
      memcmp(token, "mgst_", 5))
    return false;
  for (size_t i = 0; i < bytes; ++i)
    if ((unsigned char)token[i] < 0x21 || (unsigned char)token[i] > 0x7e)
      return false;
  portENTER_CRITICAL(&s_lock);
  bool ok = !s_status.busy && !s_status.token_loaded && (!guard || guard(guard_user));
  if (ok) {
    memcpy(s_token, token, bytes);
    s_token[bytes] = 0;
    s_status.token_loaded = true;
  }
  portEXIT_CRITICAL(&s_lock);
  return ok;
}
bool hb_muse_setup_set_local_token(const char *token, size_t bytes) {
  return hb_muse_setup_set_local_token_guarded(token, bytes, NULL, NULL);
}
bool hb_muse_setup_open(void) {
  if (!atomic_load(&s_initialized))
    return false;
  uint64_t now = s_ops.now_ms(s_ops.user);
  portENTER_CRITICAL(&s_lock);
  bool ok = !s_clock_fault && !s_status.busy && s_status.token_loaded && s_window != UINT64_MAX &&
            now <= UINT64_MAX - HB_MUSE_PAIRING_WINDOW_MS;
  if (ok) {
    ++s_window;
    s_wanted = true;
    s_deadline = now + HB_MUSE_PAIRING_WINDOW_MS;
    s_requested_ms = now;
    s_status.window = s_window;
    s_status.busy = true;
    s_status.error = HB_MUSE_SETUP_OK;
    s_status.phase = HB_MUSE_SETUP_QUIESCING;
    s_status.wifi_requested = s_status.account_stored = false;
  } else if (!s_status.token_loaded)
    s_status.error = HB_MUSE_SETUP_TOKEN_REQUIRED;
  portEXIT_CRITICAL(&s_lock);
  if (ok) {
    hb_muse_runtime_revoke();
    (void)hb_muse_runtime_set_enabled(false);
  }
  return ok;
}
bool hb_muse_setup_button(uint64_t ticket, uint64_t edge_serial, bool pressed) {
  if (!atomic_load(&s_initialized) || !ticket || !edge_serial)
    return false;
  event_t event = {0};
  portENTER_CRITICAL(&s_lock);
  bool ok = ticket == s_status.confirmation_ticket && s_status.phase == HB_MUSE_SETUP_CONFIRM;
  event.window = s_window;
  event.connection = s_connection;
  portEXIT_CRITICAL(&s_lock);
  if (!ok)
    return false;
  event.button = true;
  event.ticket = ticket;
  event.edge = edge_serial;
  event.pressed = pressed;
  event.at_ms = s_ops.now_ms(s_ops.user);
  return enqueue(&event);
}
void hb_muse_setup_revoke(void) {
  if (!atomic_load(&s_initialized))
    return;
  portENTER_CRITICAL(&s_lock);
  invalidate_locked(HB_MUSE_SETUP_OK);
  portEXIT_CRITICAL(&s_lock);
}
bool hb_muse_setup_status(hb_muse_setup_status_t *out) {
  if (!out || !atomic_load(&s_initialized))
    return false;
  portENTER_CRITICAL(&s_lock);
  *out = s_status;
  portEXIT_CRITICAL(&s_lock);
  return true;
}
bool hb_muse_setup_busy(void) {
  if (!atomic_load(&s_initialized))
    return false;
  portENTER_CRITICAL(&s_lock);
  bool busy = s_status.busy;
  portEXIT_CRITICAL(&s_lock);
  return busy;
}
bool hb_muse_setup_gap_connected(uint16_t handle, uint16_t mtu) {
  if (!atomic_load(&s_initialized) || mtu < 23)
    return false;
  uint64_t now = s_ops.now_ms(s_ops.user);
  portENTER_CRITICAL(&s_lock);
  bool ok = !s_clock_fault && s_wanted && s_active && !s_link && now >= s_requested_ms &&
            now < s_deadline && s_connection != UINT64_MAX;
  if (ok) {
    ++s_connection;
    s_handle = handle;
    s_mtu = mtu;
    s_link = true;
    s_notify = false;
  }
  portEXIT_CRITICAL(&s_lock);
  return ok;
}
void hb_muse_setup_gap_subscribed(uint16_t handle, bool notify, uint16_t mtu) {
  if (!atomic_load(&s_initialized))
    return;
  portENTER_CRITICAL(&s_lock);
  if (s_link && s_handle == handle) {
    s_notify = notify && mtu >= 23;
    if (mtu >= 23)
      s_mtu = mtu;
    if (!s_notify && s_status.confirmation_ticket)
      invalidate_locked(HB_MUSE_SETUP_PROTOCOL);
  }
  portEXIT_CRITICAL(&s_lock);
}
void hb_muse_setup_gap_disconnected(uint16_t handle) {
  if (!atomic_load(&s_initialized))
    return;
  portENTER_CRITICAL(&s_lock);
  if (s_link && s_handle == handle) {
    s_link = s_notify = false;
    s_status.confirmation_ticket = 0;
    s_status.release_seen = false;
  }
  portEXIT_CRITICAL(&s_lock);
}
bool hb_muse_setup_stack_reset(void) {
  if (!atomic_load(&s_initialized))
    return false;
  portENTER_CRITICAL(&s_lock);
  bool ok = !s_active && !s_link;
  portEXIT_CRITICAL(&s_lock);
  if (ok)
    hb_muse_pairing_gatt_stack_reset();
  return ok;
}
