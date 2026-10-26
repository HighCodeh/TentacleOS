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

#include "hb_muse_platform.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Host ABI check uses real public SDK headers, only esp_err_t is a fixture.
 * Firmware builds may enable this same check against actual IDF headers. */
#ifdef HB_MUSE_PLATFORM_CHECK_SDK_HEADERS
#include "muse_link.h"
#include "muse_settings.h"
#include "muse_state.h"
#include "muse_wifi.h"
_Static_assert(MUSE_HOST_MAX + 1u == HB_MUSE_PLATFORM_HOST_CAP, "host ABI changed");
_Static_assert(MUSE_VM_MAX + 1u == HB_MUSE_PLATFORM_VM_CAP, "VM ABI changed");
_Static_assert(MUSE_TOKEN_MAX + 1u == HB_MUSE_PLATFORM_TOKEN_CAP, "token ABI changed");
#endif

static hb_muse_platform_ops_t s_ops;
static void *s_user;
static hb_muse_platform_settings_t s_settings;
static uint64_t s_generation;
static bool s_initialized, s_enabled, s_valid, s_exhausted;

static bool terminated(const char *s, size_t cap) {
  return s && cap && memchr(s, '\0', cap) != NULL;
}
static void discard_token(char *token) {
  /* Current account provider bounds VM tokens to 2048 bytes including NUL.
   * Keep rejection cleanup bounded even if a future provider violates that
   * limit. The callback contract still requires a valid allocated C string.
   * Volatile stores prevent dead-store elimination before free(). */
  if (token) {
    size_t bytes = 0;
    while (bytes < 2048u && token[bytes])
      ++bytes;
    volatile unsigned char *p = (volatile unsigned char *)token;
    while (bytes)
      p[--bytes] = 0;
  }
  free(token);
}
static bool ready_locked(void) {
  return !s_exhausted && s_enabled && s_valid && s_settings.account_present;
}
static void advance_locked(void) {
  if (s_generation == UINT64_MAX)
    s_exhausted = true;
  else
    ++s_generation;
}
static bool snapshot_generation(uint64_t *generation) {
  if (!s_initialized)
    return false;
  s_ops.lock(s_user);
  bool ready = ready_locked();
  *generation = s_generation;
  s_ops.unlock(s_user);
  return ready;
}
static bool still_ready(uint64_t generation) {
  s_ops.lock(s_user);
  bool ready = ready_locked() && s_generation == generation;
  s_ops.unlock(s_user);
  return ready;
}
bool hb_muse_platform_init(const hb_muse_platform_ops_t *ops, void *user) {
  if (s_initialized || !ops || !ops->lock || !ops->unlock || !ops->wifi_connected ||
      !ops->load_settings || !ops->resolve_vm)
    return false;
  s_ops = *ops;
  s_user = user;
  s_generation = 1;
  s_initialized = true;
  return true;
}
bool hb_muse_platform_refresh(void) {
  if (!s_initialized)
    return false;
  s_ops.lock(s_user);
  advance_locked();
  uint64_t generation = s_generation;
  s_valid = false;
  memset(&s_settings, 0, sizeof(s_settings));
  bool exhausted = s_exhausted;
  s_ops.unlock(s_user);
  if (exhausted)
    return false;
  hb_muse_platform_settings_t settings = {0};
  bool loaded = s_ops.load_settings(s_user, &settings) &&
                terminated(settings.host, sizeof(settings.host)) &&
                terminated(settings.preferred_vm, sizeof(settings.preferred_vm));
  s_ops.lock(s_user);
  bool committed = loaded && !s_exhausted && s_generation == generation;
  if (committed) {
    s_settings = settings;
    s_valid = true;
  }
  s_ops.unlock(s_user);
  memset(&settings, 0, sizeof(settings));
  return committed;
}
void hb_muse_platform_set_enabled(bool enabled) {
  if (!s_initialized)
    return;
  s_ops.lock(s_user);
  if (s_enabled != enabled)
    advance_locked();
  s_enabled = enabled && !s_exhausted;
  s_ops.unlock(s_user);
}
void hb_muse_platform_revoke(void) {
  if (!s_initialized)
    return;
  s_ops.lock(s_user);
  advance_locked();
  s_enabled = s_valid = false;
  memset(&s_settings, 0, sizeof(s_settings));
  s_ops.unlock(s_user);
}
bool muse_link_hatch_linked(void) {
  uint64_t generation;
  return snapshot_generation(&generation);
}
bool muse_wifi_connected(void) {
  uint64_t generation;
  return snapshot_generation(&generation) && s_ops.wifi_connected(s_user) &&
         still_ready(generation);
}
bool hb_muse_platform_eligible(void) {
  return muse_wifi_connected();
}

bool muse_link_hatch_vm(const char *want_vm,
                        char *vm_id,
                        size_t id_cap,
                        char *vm_name,
                        size_t name_cap,
                        char **vm_token) {
  /* Do not overwrite/leak an existing caller-owned allocation. */
  if (!vm_token || *vm_token)
    return false;
  if (vm_id && id_cap)
    memset(vm_id, 0, id_cap);
  if (vm_name && name_cap)
    memset(vm_name, 0, name_cap);
  if (!want_vm || !vm_id || !id_cap || !vm_name || !name_cap)
    return false;
  uint64_t generation;
  if (!snapshot_generation(&generation) || !s_ops.wifi_connected(s_user) ||
      !still_ready(generation))
    return false;
  char *token = NULL;
  bool result = s_ops.resolve_vm(s_user, want_vm, vm_id, id_cap, vm_name, name_cap, &token);
  bool valid = result && token && token[0] && terminated(vm_id, id_cap) && vm_id[0] &&
               terminated(vm_name, name_cap);
  /* Always observe revocation/generation AFTER a possibly blocking lookup.
   * No provider callback runs under the cache lock. */
  valid = valid && s_ops.wifi_connected(s_user) && still_ready(generation);
  if (!valid) {
    discard_token(token);
    memset(vm_id, 0, id_cap);
    memset(vm_name, 0, name_cap);
    return false;
  }
  *vm_token = token;
  return true;
}
static void copy_setting(char *out, size_t cap, bool host) {
  if (!out)
    return;
  memset(out, 0, cap);
  if (!s_initialized)
    return;
  s_ops.lock(s_user);
  if (ready_locked())
    memcpy(out, host ? s_settings.host : s_settings.preferred_vm, cap);
  s_ops.unlock(s_user);
}
void muse_settings_hatch_host(char out[HB_MUSE_PLATFORM_HOST_CAP]) {
  copy_setting(out, HB_MUSE_PLATFORM_HOST_CAP, true);
}
void muse_settings_hatch_vm(char out[HB_MUSE_PLATFORM_VM_CAP]) {
  copy_setting(out, HB_MUSE_PLATFORM_VM_CAP, false);
}
void muse_settings_hatch_token(char out[HB_MUSE_PLATFORM_TOKEN_CAP]) {
  if (out)
    memset(out, 0, HB_MUSE_PLATFORM_TOKEN_CAP);
}
size_t muse_settings_hatch_token_len(void) {
  return 0;
}
bool muse_settings_speaker_on(void) {
  return false;
}
void muse_state_page(int *cols, int *lines) {
  if (cols)
    *cols = 16;
  if (lines)
    *lines = 2;
}
