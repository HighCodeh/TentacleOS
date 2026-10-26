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

/* Isolated High Boy platform hooks for the checked, headless Muse session.
 * No radio, SDK startup, NVS, pairing, microphone, SPI or API implementation.
 * Compile INSTEAD OF upstream main/voice_muse_chat.c, never alongside it.
 */
#ifndef HB_MUSE_PLATFORM_H
#define HB_MUSE_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define HB_MUSE_PLATFORM_HOST_CAP  64u
#define HB_MUSE_PLATFORM_VM_CAP    64u
#define HB_MUSE_PLATFORM_TOKEN_CAP 1024u

typedef struct {
  char host[HB_MUSE_PLATFORM_HOST_CAP];       /* empty uses the SDK's default */
  char preferred_vm[HB_MUSE_PLATFORM_VM_CAP]; /* empty uses account default */
  bool account_present; /* provider's actual local paired record, NOT API validation */
} hb_muse_platform_settings_t;

typedef struct {
  /* Required short cache lock. On C5 use the existing RTOS critical-section
   * mechanism, NOT a spin-until-free lock that can deadlock on one core.
   * No provider, heap, network or storage callback runs while held. */
  void (*lock)(void *user);
  void (*unlock)(void *user);
  bool (*wifi_connected)(void *user); /* read existing service only */
  /* Explicit refresh only; provider marshals storage reads to a safe
   * internal-RAM task when required. Fully initialize and NUL-terminate. */
  bool (*load_settings)(void *user, hb_muse_platform_settings_t *out);
  /* Exact upstream buffer/capacity semantics, with a context argument.
   * Success transfers a nonempty malloc/free-compatible NUL-terminated VM
   * token to the SDK caller. No token is cached here. Failure must leave
   * *vm_token NULL or transfer an allocated token for this shim to free.
   * Callback must be bounded, serialize its credential worker, support
   * revocation itself, and never retain output pointers after return.
   * This function may block OUTSIDE the cache lock. */
  bool (*resolve_vm)(void *user,
                     const char *want_vm,
                     char *vm_id,
                     size_t id_cap,
                     char *vm_name,
                     size_t name_cap,
                     char **vm_token);
} hb_muse_platform_ops_t;

/* Once, before starting readers/SDK tasks. No callback is invoked, and access
 * starts disabled/unconfigured. Reinitialization is rejected. ops/user must
 * remain valid until all readers and provider calls have stopped. */
bool hb_muse_platform_init(const hb_muse_platform_ops_t *ops, void *user);
/* Single local settings/consent owner calls these. Getters and VM lookup may
 * run on SDK tasks concurrently. Refresh invalidates cached readiness first;
 * failures stay unavailable, never silently reuse old account settings. */
bool hb_muse_platform_refresh(void);
void hb_muse_platform_set_enabled(bool enabled);
/* Clears cached settings/account presence and local enablement. In-flight
 * results are discarded. Owner must ALSO cancel/fence the session: these
 * hooks cannot retract a previous upload or erase the SDK's private cache. */
void hb_muse_platform_revoke(void);
/* Prerequisites only, NOT an established Noise session or API authentication. */
bool hb_muse_platform_eligible(void);

/* Upstream ABI. Legacy hook names are required by the pinned SDK. */
bool muse_link_hatch_linked(void);
bool muse_link_hatch_vm(const char *want_vm,
                        char *vm_id,
                        size_t id_cap,
                        char *vm_name,
                        size_t name_cap,
                        char **vm_token);
bool muse_wifi_connected(void);
void muse_settings_hatch_host(char out[HB_MUSE_PLATFORM_HOST_CAP]);
void muse_settings_hatch_vm(char out[HB_MUSE_PLATFORM_VM_CAP]);
void muse_settings_hatch_token(char out[HB_MUSE_PLATFORM_TOKEN_CAP]);
size_t muse_settings_hatch_token_len(void);
bool muse_settings_speaker_on(void); /* false: checked port is text-only */
void muse_state_page(int *cols, int *lines);

#ifdef __cplusplus
}
#endif
#endif
