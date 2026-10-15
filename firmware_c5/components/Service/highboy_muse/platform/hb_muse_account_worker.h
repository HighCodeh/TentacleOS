/* C5 credential worker for the checked Muse session; not a startup routine.
 * No app_run, automatic radio/pairing/reset/erase or credential logging.
 */
#ifndef HB_MUSE_ACCOUNT_WORKER_H
#define HB_MUSE_ACCOUNT_WORKER_H
#include "hb_muse_account_fetch.h"
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Once, before any reader. Provider pointers/context remain valid for the
 * lifetime of the service. This does not create a task or read NVS/network.
 * All account settings/identity mutations MUST disable/revoke this service
 * and wait for idle before writing, then refresh the platform settings cache.
 */
bool hb_muse_account_worker_init(const hb_muse_account_ops_t *ops, void *user);
/* Actual local facts; revoke changes generation even when already disabled.
 * Neither function waits for network or claims remote cancellation. */
void hb_muse_account_worker_access(bool enabled, bool paired, bool network);
void hb_muse_account_worker_revoke(void);
bool hb_muse_account_worker_idle(void);
/* Upstream-compatible blocking resolver. At most one internal-RAM worker.
 * Caller waits <=15s (plus RTOS scheduling), never portMAX_DELAY. A timeout
 * abandons an owned heap request, not a stack pointer; the worker may finish
 * its existing HTTP operation later but cannot publish stale credentials.
 * Concurrent requests fail without starting another worker. Caller must set
 * *vm_token=NULL; an existing token is rejected untouched. Returned token on
 * success is caller-owned/free()-compatible. Other failures clear outputs.
 */
bool hb_muse_account_worker_lookup(void *unused,
                                   const char *want_vm,
                                   char *vm_id,
                                   size_t id_cap,
                                   char *vm_name,
                                   size_t name_cap,
                                   char **vm_token);
#ifdef __cplusplus
}
#endif
#endif
