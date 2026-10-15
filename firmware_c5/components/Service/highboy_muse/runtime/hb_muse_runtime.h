/* Experimental C5 service-owner glue, not installed in TentacleOS.
 * Uses existing Wi-Fi/NVS; never initializes radio, BLE, pairing or microphone.
 * Local frame queues are not an authenticated network interface. */
#ifndef HB_MUSE_RUNTIME_H
#define HB_MUSE_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define HB_MUSE_RUNTIME_FRAME_MAX   248u
#define HB_MUSE_RUNTIME_QUEUE_SLOTS 4u
#define HB_MUSE_RUNTIME_SESSION_MS  120000u

typedef struct {
  uint32_t boot_id;                   /* genuinely new, nonzero ID on every C5 boot */
  const char *existing_device_id;     /* copied; no new identity is generated */
  bool (*wifi_connected)(void *user); /* existing service, read-only */
  uint64_t (*now_ms)(void *user);     /* monotonic, never wall clock */
  void *user;                         /* callbacks/context remain alive for service lifetime */
} hb_muse_runtime_config_t;
typedef enum {
  HB_MUSE_RUNTIME_INITIALIZING,
  HB_MUSE_RUNTIME_DISABLED,
  HB_MUSE_RUNTIME_UNPAIRED,
  HB_MUSE_RUNTIME_OFFLINE,
  HB_MUSE_RUNTIME_PREPARED,
  HB_MUSE_RUNTIME_STARTING,
  HB_MUSE_RUNTIME_CONNECTING,
  HB_MUSE_RUNTIME_ONLINE,
  HB_MUSE_RUNTIME_QUIESCING,
  HB_MUSE_RUNTIME_FAULT
} hb_muse_runtime_phase_t;
typedef enum {
  HB_MUSE_RUNTIME_NO_ERROR,
  HB_MUSE_RUNTIME_INIT_ERROR,
  HB_MUSE_RUNTIME_STORE_ERROR,
  HB_MUSE_RUNTIME_START_ERROR,
  HB_MUSE_RUNTIME_CONTROL_ERROR,
  HB_MUSE_RUNTIME_CLOCK_ERROR,
  HB_MUSE_RUNTIME_LINK_EXPIRED,
  HB_MUSE_RUNTIME_SESSION_EXPIRED
} hb_muse_runtime_error_t;
typedef struct {
  hb_muse_runtime_phase_t phase;
  hb_muse_runtime_error_t error;
  bool enabled, paired, network, cloud_authorized, worker_started, session_ready, fenced;
  uint32_t session_requested_revision, session_applied_revision;
  uint32_t accepted_frames, rejected_frames, processed_frames;
} hb_muse_runtime_status_t;

/* Once before callers; allocates two bounded index queues and an 8192-byte
 * INTERNAL-RAM owner task. Returns local task creation, NOT SDK readiness.
 * Owner initializes store/platform/account/endpoint, all disabled. No NVS
 * read or SDK start until explicit enable/connect. Existing NVS must already
 * be initialized; verified pairing and approved SDK API configuration remain
 * separate prerequisites. No other owner may initialize/use this Muse SDK.
 * Partial component-init/SDK-start failure is latched; reboot is required.
 */
bool hb_muse_runtime_init(const hb_muse_runtime_config_t *config);
/* One trusted local consent owner. Enabling only reads cached public account
 * settings on the internal-stack owner; it does NOT connect to the cloud.
 * CONNECT is separate, requires actual paired record + existing Wi-Fi, and
 * grants at most 120 seconds. Network loss/lease expiry requires a new CONNECT.
 */
bool hb_muse_runtime_set_enabled(bool enabled);
bool hb_muse_runtime_connect(void);
/* Out-of-band revocation: cannot be blocked by a full ingress queue. It clears
 * credential permission immediately; the owner revokes endpoint input and
 * requests actual SDK teardown on its next bounded scheduling pass. QUIESCING
 * is retained until SDK fence + account worker idle; timeout is never a fence.
 */
void hb_muse_runtime_revoke(void);
/* One local ingress producer and one reply consumer, task context only.
 * False submit consumes nothing. Caller must retry exact wire sequence if a
 * reply is lost. Output capacity >=248 is required BEFORE removing a reply.
 * Payload slots are wiped when consumed/dropped; RTOS queues hold indices only.
 */
bool hb_muse_runtime_submit(const uint8_t *frame, size_t bytes);
bool hb_muse_runtime_take_reply(uint8_t *frame, size_t capacity, size_t *bytes);
bool hb_muse_runtime_status(hb_muse_runtime_status_t *status);
#ifdef HB_MUSE_RUNTIME_HOST_TEST
/* Deterministic host scheduler invokes the same owner initialization/step.
 * Not part of the firmware API and never a replacement for RTOS scheduling. */
void hb_muse_runtime_host_step(void);
#endif
#ifdef __cplusplus
}
#endif
#endif
