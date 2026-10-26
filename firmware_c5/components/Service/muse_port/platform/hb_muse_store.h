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

/* Isolated C5 NVS account provider. Uses the EXISTING initialized NVS service.
 * Never initializes/erases NVS, touches eFuses, starts radio or logs secrets.
 * Initial import is conditional: the local BLE owner must verify fresh physical
 * confirmation, completed SDK/account quiescence and stable connection identity.
 */
#ifndef HB_MUSE_STORE_H
#define HB_MUSE_STORE_H
#include "hb_muse_account_fetch.h"
#include "hb_muse_platform.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define HB_MUSE_STORE_MAGIC UINT32_C(0x48424d31)
/* A versioned whole-record blob; never two independently committed tokens.
 * The persisted namespace is hb_muse/key account, not existing Wi-Fi assets.
 * Record format is private to this C5 port and not a Muse pairing packet. */
typedef struct {
  uint32_t magic;
  uint32_t version;
  char device_id[HB_MUSE_ACCOUNT_ID_CAP];
  char access[HB_MUSE_ACCOUNT_TOKEN_CAP];
  char refresh[HB_MUSE_ACCOUNT_TOKEN_CAP];
  char preferred_url[HB_MUSE_ACCOUNT_URL_CAP];
  char host[HB_MUSE_PLATFORM_HOST_CAP];
  char preferred_vm[HB_MUSE_PLATFORM_VM_CAP];
} hb_muse_store_record_t;

/* Called on internal-RAM stack before other users. Creates only a mutex and
 * copies the existing stable identity; does not read/write flash or hardware.
 * The current callback is a cached, nonblocking consent/account/network check.
 */
bool hb_muse_store_init(const char *existing_device_id, bool (*current)(void *), void *user);
const hb_muse_account_ops_t *hb_muse_store_account_ops(void);
/* Internal-stack settings read; missing account returns valid unpaired state.
 * Storage errors/malformed or foreign-identity record return false. */
bool hb_muse_store_load_settings(void *unused, hb_muse_platform_settings_t *out);
/* Initial account ONLY, never replacement/migration/reset. Refuses any existing
 * record, even malformed. Cloud permission must be false; guard must prove the
 * current confirmed BLE generation AND runtime fence/account idle throughout.
 * Called on INTERNAL-RAM owner stack; guard nonblocking and not reentrant.
 * Host and URLs are pinned to defaults: empty host or hatch.metaaivm.com;
 * empty preferred_url/preferred_vm. Whole-record commit + verified readback.
 * Failure after attempted staging is latched for this boot. Caller-owned input
 * remains caller's responsibility to wipe. No SDK maker token stored here.
 */
bool hb_muse_store_import_initial(const hb_muse_store_record_t *record,
                                  bool (*confirmed_and_quiet)(void *),
                                  void *user);
#ifdef __cplusplus
}
#endif
#endif
