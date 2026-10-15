/* C5-only Muse setup owner. Local physical consent, not an Internet endpoint.
 * All public functions are task-context only. No token getter/log/NVS token.
 * Existing BT owner retains exclusive NimBLE ownership; no second host. */
#ifndef HB_MUSE_SETUP_H
#define HB_MUSE_SETUP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hb_muse_pairing.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HB_MUSE_SETUP_RX_SLOTS  8u
#define HB_MUSE_SETUP_TOKEN_CAP 1024u
typedef enum {
  HB_MUSE_SETUP_OFF,
  HB_MUSE_SETUP_QUIESCING,
  HB_MUSE_SETUP_ADVERTISING,
  HB_MUSE_SETUP_PHONE,
  HB_MUSE_SETUP_CONFIRM,
  HB_MUSE_SETUP_CONFIRMED,
  HB_MUSE_SETUP_SAVED,
  HB_MUSE_SETUP_FAILED
} hb_muse_setup_phase_t;
typedef enum {
  HB_MUSE_SETUP_OK,
  HB_MUSE_SETUP_TOKEN_REQUIRED,
  HB_MUSE_SETUP_BUSY,
  HB_MUSE_SETUP_TIMEOUT,
  HB_MUSE_SETUP_CLOCK,
  HB_MUSE_SETUP_BT,
  HB_MUSE_SETUP_OVERFLOW,
  HB_MUSE_SETUP_PROTOCOL,
  HB_MUSE_SETUP_STORE,
  HB_MUSE_SETUP_ALREADY_PAIRED,
  HB_MUSE_SETUP_WIFI,
  HB_MUSE_SETUP_RUNTIME
} hb_muse_setup_error_t;
typedef struct {
  uint64_t (*now_ms)(void *user);
  /* Reserve existing BT exclusively BEFORE stopping/reinitializing it.
   * While held, other BT/NimBLE owners and runtime enable/connect are denied.
   * Call registration hook after NimBLE init, before starting that host.
   * Return false with reservation released on any failure. */
  bool (*acquire_ble)(void *user, bool (*register_service)(void));
  /* Actual SDK service UUID, connectable. GAP callbacks must route below.
   * No auto-advertise after disconnect outside this setup owner's window. */
  bool (*advertise)(void *user);
  /* Stop advertisements + disconnect all setup links BEFORE releasing
   * reservation. Must serialize against late GAP callbacks/other BT owners. */
  void (*release_ble)(void *user);
  /* Nonblocking gate: Grok and competing sessions stopped; reservation is
   * still ours. Never mutates anything from a pairing/store guard. */
  bool (*services_quiet)(void *user);
  /* Existing Wi-Fi service only. Recheck guard under its config/store lock
   * BEFORE save/configure/connect and after. Return true only on verified
   * save + actual association/IP for this SSID, bounded to <15s. No API,
   * erase or fallback. False on revocation; never claim network readiness.
   * Wi-Fi and account are distinct commits, NOT an atomic transaction. */
  bool (*wifi_provision)(
      void *user, const char *ssid, const char *password, bool (*guard)(void *), void *guard_user);
  void *user;
  const char *node_id, *device_id, *mac, *firmware_version;
  /* Actual bounded Wi-Fi survey, only after phone physical confirmation.
   * See pairing provider contract; absent provider fails closed. */
  bool (*wifi_scan)(void *user,
                    hb_muse_pairing_network_t *out,
                    size_t capacity,
                    size_t *count,
                    bool (*guard)(void *),
                    void *guard_user);
} hb_muse_setup_config_t;
typedef struct {
  hb_muse_setup_phase_t phase;
  hb_muse_setup_error_t error;
  uint64_t window, confirmation_ticket;
  bool token_loaded, busy, subscribed, release_seen, wifi_requested, account_stored;
  uint32_t dropped;
} hb_muse_setup_status_t;
bool hb_muse_setup_init(const hb_muse_setup_config_t *config);
/* ONLY a hidden, local C5 console reader may call this. Never accept via P4,
 * BLE or network; no argv/history. Buffer is copied, caller must wipe it.
 * Exactly one token per boot; no replacement after SDK initialization. */
bool hb_muse_setup_set_local_token(const char *token, size_t bytes);
/* UART transfer uses this variant. guard executes INSIDE setup's short lock:
 * atomic cancellation revision + monotonic deadline ONLY; no other lock,
 * allocation, logging, setup calls or callbacks. Busy is checked internally. */
bool hb_muse_setup_set_local_token_guarded(const char *token,
                                           size_t bytes,
                                           bool (*guard)(void *),
                                           void *guard_user);
/* Local/P4 explicit setup request only; no advertising until token + runtime
 * fence + unpaired store + existing-BT exclusive acquisition are verified. */
bool hb_muse_setup_open(void);
bool hb_muse_setup_button(uint64_t ticket, uint64_t edge_serial, bool pressed);
void hb_muse_setup_revoke(void); /* immediate guards; deferred orderly teardown */
bool hb_muse_setup_status(hb_muse_setup_status_t *status);
bool hb_muse_setup_busy(void);
/* Existing single NimBLE GAP owner routes events synchronously. Reject a
 * second central. Failed connect must be terminated by GAP owner. */
bool hb_muse_setup_gap_connected(uint16_t handle, uint16_t mtu);
void hb_muse_setup_gap_subscribed(uint16_t handle, bool notify, uint16_t mtu);
void hb_muse_setup_gap_disconnected(uint16_t handle);
/* Call ONLY after actual NimBLE host stop/deinit, before a fresh registration.
 * Refuses while setup has any active window. */
bool hb_muse_setup_stack_reset(void);
#ifdef HB_MUSE_SETUP_HOST_TEST
void hb_muse_setup_host_step(void);
#endif
#ifdef __cplusplus
}
#endif
#endif
