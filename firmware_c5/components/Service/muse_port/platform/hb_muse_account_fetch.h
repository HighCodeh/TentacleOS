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

/* Isolated account lookup for an INTERNAL-RAM-stack worker only. This is not
 * provisioning and does not start Wi-Fi/BLE, pair, erase, migrate or mint.
 * Do not log tokens or pass them to P4/UI. Only an explicitly authorized runtime
 * may invoke this function; host tests replace API functions and perform no I/O.
 */
#ifndef HB_MUSE_ACCOUNT_FETCH_H
#define HB_MUSE_ACCOUNT_FETCH_H
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define HB_MUSE_ACCOUNT_ID_CAP    128u
#define HB_MUSE_ACCOUNT_NAME_CAP  64u
#define HB_MUSE_ACCOUNT_TOKEN_CAP 2048u /* Upstream TOKEN_BUF_BYTES. */
#define HB_MUSE_ACCOUNT_URL_CAP   256u

typedef enum {
  HB_MUSE_ACCOUNT_OK,
  HB_MUSE_ACCOUNT_UNAVAILABLE,
  HB_MUSE_ACCOUNT_AUTH,
  HB_MUSE_ACCOUNT_IO,
  HB_MUSE_ACCOUNT_NO_VM,
  HB_MUSE_ACCOUNT_STALE,
  HB_MUSE_ACCOUNT_OOM
} hb_muse_account_status_t;
typedef enum {
  HB_MUSE_ACCOUNT_READ_OK,
  HB_MUSE_ACCOUNT_READ_MISSING,
  HB_MUSE_ACCOUNT_READ_ERROR
} hb_muse_account_read_t;
typedef struct {
  /* Read a COMPLETE NUL-terminated value or return ERROR; never truncate.
   * MISSING is distinct from storage failure. Only vm_url/access_token/
   * refresh_token are read. No legacy credential fallback is performed.
   */
  hb_muse_account_read_t (*read_str)(void *user, const char *key, char *output, size_t capacity);
  /* Must include consent, paired identity, network and request generation.
   * Called before/after every API operation and durable pair commit.
   */
  bool (*current)(void *user);
  /* Conditional durable ATOMIC pair replacement, serialized by provider
   * against pairing changes and checking the same request generation INSIDE
   * its commit critical section. Return true only once durable. Must not
   * mutate identity, trigger pairing or advance this request's generation.
   * Call still_current(current_user) inside the provider's pairing mutex,
   * before staging/committing and after commit. A check before this callback
   * alone cannot close the pairing race. Do not retain the guard/user.
   */
  bool (*store_pair)(void *user,
                     const char *access,
                     const char *refresh,
                     bool (*still_current)(void *),
                     void *current_user);
  /* Existing stable device identity only: bounded complete NUL string.
   * Must not generate a new identity. Required only after access AUTH.
   */
  bool (*identity)(void *user, char *output, size_t capacity);
} hb_muse_account_ops_t;
typedef struct {
  char id[HB_MUSE_ACCOUNT_ID_CAP];
  char name[HB_MUSE_ACCOUNT_NAME_CAP];
  char *token; /* caller owns on OK; clear with result_clear(). */
} hb_muse_account_result_t;

/* Initialize result to {0}; clear an earlier result before reuse. Ordinary
 * failure leaves it empty; passing a nonempty result is rejected unchanged
 * so the caller retains ownership. want_vm is an exact VM ID, NOT a URL. When absent,
 * configured vm_url is exact and strict; only an absent/empty preference uses
 * the SDK default selection (first entry when the API marks none default).
 * One fetch; only AUTH allows one refresh-token-only refresh and one refetch.
 * Refresh must be durably committed before refetch. No API base override here;
 * runtime initializes the SDK's approved API base/token configuration explicitly.
 * Calls are synchronous: the SDK may retry I/O internally. current() is checked
 * between calls, not an in-flight TLS cancellation primitive.
 */
hb_muse_account_status_t hb_muse_account_fetch(const hb_muse_account_ops_t *ops,
                                               void *user,
                                               const char *want_vm,
                                               hb_muse_account_result_t *result);
void hb_muse_account_result_clear(hb_muse_account_result_t *result);

#ifdef __cplusplus
}
#endif
#endif
