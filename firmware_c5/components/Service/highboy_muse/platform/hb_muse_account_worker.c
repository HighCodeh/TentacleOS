#include "hb_muse_account_worker.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef HB_MUSE_WORKER_HOST_TEST
#include "account_worker_host.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#endif

#ifndef HB_MUSE_ACCOUNT_WAIT_MS
#define HB_MUSE_ACCOUNT_WAIT_MS 15000u
#endif
#if HB_MUSE_ACCOUNT_WAIT_MS > 15000 || HB_MUSE_ACCOUNT_WAIT_MS < 1
#error Invalid credential wait budget
#endif

typedef struct {
  atomic_uint references;
  atomic_bool abandoned;
  uint32_t generation;
  char want[128];
  SemaphoreHandle_t completed;
  hb_muse_account_result_t result;
  hb_muse_account_status_t status;
} account_request_t;

static hb_muse_account_ops_t s_ops;
static void *s_user;
static bool s_initialized;
static atomic_bool s_busy;
static atomic_bool s_exhausted;
static atomic_uint s_access;
static atomic_uint s_generation = 1;

static void wipe(void *buffer, size_t size) {
  volatile unsigned char *p = buffer;
  while (size--)
    *p++ = 0;
}
static void release(account_request_t *r) {
  if (atomic_fetch_sub_explicit(&r->references, 1, memory_order_acq_rel) != 1)
    return;
  hb_muse_account_result_clear(&r->result);
  vSemaphoreDeleteWithCaps(r->completed);
  wipe(r, sizeof(*r));
  heap_caps_free(r);
}
static bool current(void *user) {
  account_request_t *r = user;
  return !atomic_load(&r->abandoned) && !atomic_load(&s_exhausted) &&
         atomic_load(&s_access) == 7u && atomic_load(&s_generation) == r->generation &&
         s_ops.current(s_user);
}
static hb_muse_account_read_t read_str(void *user, const char *key, char *out, size_t capacity) {
  if (!current(user))
    return HB_MUSE_ACCOUNT_READ_ERROR;
  return s_ops.read_str(s_user, key, out, capacity);
}
static bool store_pair(void *user,
                       const char *access,
                       const char *refresh,
                       bool (*still_current)(void *),
                       void *current_user) {
  /* Provider commit is conditional/durable and serialized with pairing.
   * No new identity may be installed until this worker has become idle. */
  return still_current(current_user) && current(user) &&
         s_ops.store_pair(s_user, access, refresh, current, user) && current(user);
}
static bool identity(void *user, char *out, size_t capacity) {
  return current(user) && s_ops.identity(s_user, out, capacity);
}
static void account_task(void *user) {
  account_request_t *r = user;
  const hb_muse_account_ops_t guarded = {read_str, current, store_pair, identity};
  r->status = hb_muse_account_fetch(&guarded, r, r->want, &r->result);
  if (!current(r)) {
    hb_muse_account_result_clear(&r->result);
    r->status = HB_MUSE_ACCOUNT_STALE;
  }
  /* Semaphore owns completion ordering; caller/worker each retain one ref.
   * Notification after caller timeout is harmless and cannot use its stack. */
  (void)xSemaphoreGive(r->completed);
  release(r);
  atomic_store(&s_busy, false);
  vTaskDeleteWithCaps(NULL);
}
bool hb_muse_account_worker_init(const hb_muse_account_ops_t *ops, void *user) {
  if (s_initialized || !ops || !ops->read_str || !ops->current || !ops->store_pair ||
      !ops->identity)
    return false;
  s_ops = *ops;
  s_user = user;
  s_initialized = true;
  return true;
}
static void invalidate(void) {
  unsigned old = atomic_load(&s_generation);
  for (;;) {
    if (old == UINT32_MAX) {
      atomic_store(&s_exhausted, true);
      atomic_store(&s_access, 0);
      return;
    }
    if (atomic_compare_exchange_weak(&s_generation, &old, old + 1u))
      return;
  }
}
void hb_muse_account_worker_access(bool enabled, bool paired, bool network) {
  if (!s_initialized || atomic_load(&s_exhausted))
    return;
  unsigned next = (enabled ? 1u : 0u) | (paired ? 2u : 0u) | (network ? 4u : 0u);
  unsigned old = atomic_load(&s_access);
  if (old == next)
    return;
  /* One service/consent owner serializes access/revoke calls. Clear before
   * changing generation: readers cannot inherit permission across epochs. */
  atomic_store(&s_access, 0);
  invalidate();
  if (!atomic_load(&s_exhausted))
    atomic_store(&s_access, next);
}
void hb_muse_account_worker_revoke(void) {
  if (!s_initialized)
    return;
  atomic_store(&s_access, 0);
  invalidate();
}
bool hb_muse_account_worker_idle(void) {
  return s_initialized && !atomic_load(&s_busy);
}
bool hb_muse_account_worker_lookup(void *unused,
                                   const char *want,
                                   char *id,
                                   size_t id_cap,
                                   char *name,
                                   size_t name_cap,
                                   char **token) {
  (void)unused;
  if (token && *token)
    return false; /* Never leak/replace an owned token. */
  if (id && id_cap)
    id[0] = '\0';
  if (name && name_cap)
    name[0] = '\0';
  if (token)
    *token = NULL;
  uint32_t generation = atomic_load(&s_generation);
  if (!s_initialized || !id || !id_cap || !name || !name_cap || !token ||
      atomic_load(&s_exhausted) || atomic_load(&s_access) != 7u ||
      generation != atomic_load(&s_generation))
    return false;
  size_t want_len = 0;
  if (want)
    while (want_len < 128u && want[want_len])
      ++want_len;
  if (want_len >= 128u)
    return false;
  bool expected = false;
  if (!atomic_compare_exchange_strong(&s_busy, &expected, true))
    return false;
  account_request_t *r = heap_caps_calloc(1, sizeof(*r), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!r) {
    atomic_store(&s_busy, false);
    return false;
  }
  r->completed = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!r->completed) {
    heap_caps_free(r);
    atomic_store(&s_busy, false);
    return false;
  }
  atomic_init(&r->references, 2);
  atomic_init(&r->abandoned, false);
  r->generation = generation;
  if (want_len)
    memcpy(r->want, want, want_len);
  r->status = HB_MUSE_ACCOUNT_UNAVAILABLE;
  if (!current(r) ||
      xTaskCreateWithCaps(
          account_task, "muse_account", 8192, r, 4, NULL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) !=
          pdPASS) {
    release(r);
    release(r);
    atomic_store(&s_busy, false);
    return false;
  }
  bool ok = xSemaphoreTake(r->completed, pdMS_TO_TICKS(HB_MUSE_ACCOUNT_WAIT_MS)) == pdTRUE;
  if (!ok)
    atomic_store(&r->abandoned, true);
  ok = ok && current(r) && r->status == HB_MUSE_ACCOUNT_OK && r->result.token;
  if (ok) {
    size_t ni = strlen(r->result.id), nn = strlen(r->result.name);
    if (!ni || ni >= id_cap || nn >= name_cap)
      ok = false;
    else {
      memcpy(id, r->result.id, ni + 1);
      memcpy(name, r->result.name, nn + 1);
      *token = r->result.token;
      r->result.token = NULL;
    }
  }
  release(r);
  return ok;
}
