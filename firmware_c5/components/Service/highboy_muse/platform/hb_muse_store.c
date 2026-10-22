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

#include "hb_muse_store.h"
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"

static SemaphoreHandle_t s_mutex;
static char s_device[HB_MUSE_ACCOUNT_ID_CAP];
static bool (*s_current)(void *);
static void *s_user;
static atomic_bool s_fault;
static void store_wipe(void *buffer, size_t bytes) {
  volatile unsigned char *p = buffer;
  while (bytes--)
    *p++ = 0;
}
static bool terminated(const char *s, size_t cap, bool required) {
  if (!s || (required && !s[0]))
    return false;
  for (size_t i = 0; i < cap; ++i)
    if (!s[i])
      return true;
  return false;
}
static bool store_alnum(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
static bool store_valid_token(const char *token, size_t cap) {
  if (!terminated(token, cap, true))
    return false;
  for (size_t i = 0; token[i]; ++i) {
    unsigned char c = (unsigned char)token[i];
    if (c < 0x21u || c > 0x7eu)
      return false;
  }
  /* HTTP prefix-specific length limits remain in the account engine. */
  return true;
}
static bool store_valid_host(const char *host, size_t cap) {
  if (!terminated(host, cap, false))
    return false;
  if (!host[0])
    return true;
  size_t label = 0;
  unsigned char previous = 0;
  for (size_t i = 0; host[i]; ++i) {
    unsigned char c = (unsigned char)host[i];
    if (c == '.') {
      if (!label || previous == '-')
        return false;
      label = 0;
    } else {
      if ((!store_alnum(c) && c != '-') || (!label && c == '-') || ++label > 63u)
        return false;
    }
    previous = c;
  }
  return label != 0 && previous != '-';
}
static bool store_valid_vm(const char *vm, size_t cap) {
  if (!terminated(vm, cap, false))
    return false;
  for (size_t i = 0; vm[i]; ++i)
    if (!store_alnum((unsigned char)vm[i]) && vm[i] != '-')
      return false;
  return true;
}
static bool record_valid(const hb_muse_store_record_t *r) {
  return r->magic == HB_MUSE_STORE_MAGIC && r->version == 1 &&
         terminated(r->device_id, sizeof(r->device_id), true) && !strcmp(r->device_id, s_device) &&
         store_valid_token(r->access, sizeof(r->access)) &&
         store_valid_token(r->refresh, sizeof(r->refresh)) &&
         terminated(r->preferred_url, sizeof(r->preferred_url), false) &&
         store_valid_host(r->host, sizeof(r->host)) &&
         store_valid_vm(r->preferred_vm, sizeof(r->preferred_vm));
}
static bool take(void) {
  return s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(250)) == pdTRUE;
}
static bool available(void) {
  return s_mutex && !atomic_load(&s_fault) && s_current && s_current(s_user);
}
static esp_err_t load(hb_muse_store_record_t *r) {
  nvs_handle_t h;
  esp_err_t err = nvs_open("hb_muse", NVS_READONLY, &h);
  if (err != ESP_OK)
    return err;
  size_t bytes = sizeof(*r);
  err = nvs_get_blob(h, "account", r, &bytes);
  nvs_close(h);
  if (err == ESP_OK && (bytes != sizeof(*r) || !record_valid(r)))
    return ESP_ERR_INVALID_STATE;
  return err;
}
static void destroy(hb_muse_store_record_t *r) {
  if (r) {
    store_wipe(r, sizeof(*r));
    free(r);
  }
}
bool hb_muse_store_init(const char *id, bool (*current)(void *), void *user) {
  if (s_mutex || !current || !id || !id[0])
    return false;
  size_t n = 0;
  while (n < sizeof(s_device) && id[n])
    ++n;
  if (n == sizeof(s_device))
    return false;
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
      return false;
  }
  s_mutex = xSemaphoreCreateMutexWithCaps(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_mutex)
    return false;
  memcpy(s_device, id, n + 1);
  s_current = current;
  s_user = user;
  return true;
}
static hb_muse_account_read_t
store_read(void *unused, const char *key, char *out, size_t capacity) {
  (void)unused;
  if (!key || !out || !capacity)
    return HB_MUSE_ACCOUNT_READ_ERROR;
  out[0] = '\0';
  if (!available() || !take())
    return HB_MUSE_ACCOUNT_READ_ERROR;
  hb_muse_store_record_t *r = calloc(1, sizeof(*r));
  esp_err_t err = r && available() ? load(r) : ESP_ERR_INVALID_STATE;
  hb_muse_account_read_t status = HB_MUSE_ACCOUNT_READ_ERROR;
  if (err == ESP_ERR_NVS_NOT_FOUND)
    status = HB_MUSE_ACCOUNT_READ_MISSING;
  if (err == ESP_OK) {
    const char *value = !strcmp(key, "access_token")    ? r->access
                        : !strcmp(key, "refresh_token") ? r->refresh
                        : !strcmp(key, "vm_url")        ? r->preferred_url
                                                        : NULL;
    if (value && strlen(value) < capacity && available()) {
      memcpy(out, value, strlen(value) + 1);
      status = HB_MUSE_ACCOUNT_READ_OK;
    }
  }
  destroy(r);
  (void)xSemaphoreGive(s_mutex);
  return status;
}
static bool store_current(void *unused) {
  (void)unused;
  return available();
}
static bool store_identity(void *unused, char *out, size_t capacity) {
  (void)unused;
  if (!out || !capacity)
    return false;
  out[0] = '\0';
  if (!available() || strlen(s_device) >= capacity)
    return false;
  memcpy(out, s_device, strlen(s_device) + 1);
  return true;
}
static bool store_pair(void *unused,
                       const char *access,
                       const char *refresh,
                       bool (*still_current)(void *),
                       void *current_user) {
  (void)unused;
  if (!store_valid_token(access, HB_MUSE_ACCOUNT_TOKEN_CAP) ||
      !store_valid_token(refresh, HB_MUSE_ACCOUNT_TOKEN_CAP) || !still_current ||
      !still_current(current_user) || !available() || !take())
    return false;
  hb_muse_store_record_t *r = calloc(1, sizeof(*r));
  hb_muse_store_record_t *verify = calloc(1, sizeof(*verify));
  bool ok = r && verify && available() && still_current(current_user) && load(r) == ESP_OK;
  if (ok) {
    memset(r->access, 0, sizeof(r->access));
    memset(r->refresh, 0, sizeof(r->refresh));
    memcpy(r->access, access, strlen(access) + 1);
    memcpy(r->refresh, refresh, strlen(refresh) + 1);
    nvs_handle_t h;
    ok = available() && still_current(current_user) &&
         nvs_open("hb_muse", NVS_READWRITE, &h) == ESP_OK;
    if (ok) {
      ok = available() && still_current(current_user) &&
           nvs_set_blob(h, "account", r, sizeof(*r)) == ESP_OK;
      if (ok)
        ok = still_current(current_user) && nvs_commit(h) == ESP_OK;
      nvs_close(h);
    }
    if (ok)
      ok = load(verify) == ESP_OK && !memcmp(r, verify, sizeof(*r)) && available() &&
           still_current(current_user);
    /* Any uncertain durable outcome remains faulted for this boot; no
     * auto-erase, old-token fallback, retry loop or pairing reset. */
    if (!ok)
      atomic_store(&s_fault, true);
  }
  destroy(verify);
  destroy(r);
  (void)xSemaphoreGive(s_mutex);
  return ok;
}
const hb_muse_account_ops_t *hb_muse_store_account_ops(void) {
  static const hb_muse_account_ops_t ops = {store_read, store_current, store_pair, store_identity};
  return &ops;
}
bool hb_muse_store_import_initial(const hb_muse_store_record_t *input,
                                  bool (*guard)(void *),
                                  void *user) {
  if (!s_mutex || atomic_load(&s_fault) || !s_current || s_current(s_user) || !input ||
      !record_valid(input) || input->preferred_url[0] || input->preferred_vm[0] ||
      (input->host[0] && strcmp(input->host, "hatch.metaaivm.com")) || !guard || !guard(user) ||
      !take())
    return false;
  hb_muse_store_record_t *record = calloc(1, sizeof(*record));
  hb_muse_store_record_t *verify = calloc(1, sizeof(*verify));
  bool ok = record && verify && !atomic_load(&s_fault) && !s_current(s_user) && guard(user);
  if (ok) {
    memcpy(record, input, sizeof(*record));
    /* An existing/malformed/inaccessible record is never overwritten. */
    ok = record_valid(record) && !record->preferred_url[0] && !record->preferred_vm[0] &&
         (!record->host[0] || !strcmp(record->host, "hatch.metaaivm.com")) &&
         load(verify) == ESP_ERR_NVS_NOT_FOUND;
  }
  bool attempted = false;
  if (ok && !s_current(s_user) && guard(user)) {
    nvs_handle_t h;
    ok = nvs_open("hb_muse", NVS_READWRITE, &h) == ESP_OK;
    if (ok) {
      ok = !s_current(s_user) && guard(user);
      if (ok) {
        attempted = true;
        ok = nvs_set_blob(h, "account", record, sizeof(*record)) == ESP_OK;
      }
      if (ok)
        ok = !s_current(s_user) && guard(user) && nvs_commit(h) == ESP_OK;
      nvs_close(h);
    }
  } else
    ok = false;
  if (ok)
    ok = load(verify) == ESP_OK && !memcmp(record, verify, sizeof(*record)) && !s_current(s_user) &&
         guard(user);
  if (!ok && attempted)
    atomic_store(&s_fault, true);
  destroy(verify);
  destroy(record);
  (void)xSemaphoreGive(s_mutex);
  return ok;
}
bool hb_muse_store_load_settings(void *unused, hb_muse_platform_settings_t *out) {
  (void)unused;
  if (!out)
    return false;
  memset(out, 0, sizeof(*out));
  if (!s_mutex || atomic_load(&s_fault) || !take())
    return false;
  hb_muse_store_record_t *r = calloc(1, sizeof(*r));
  esp_err_t err = r ? load(r) : ESP_ERR_NO_MEM;
  bool ok = err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
  if (err == ESP_OK) {
    memcpy(out->host, r->host, sizeof(out->host));
    memcpy(out->preferred_vm, r->preferred_vm, sizeof(out->preferred_vm));
    out->account_present = true;
  }
  destroy(r);
  (void)xSemaphoreGive(s_mutex);
  return ok;
}
