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

/* Isolated passive Muse community-v5 setup adapter. No NimBLE/radio startup.
 * Exactly one serialized C5 INTERNAL-RAM owner calls this API; callbacks must
 * not re-enter it. The SDK pairing singleton must have no other owner.
 * BLE callbacks queue whole messages to this owner, not execute crypto in ISR.
 */
#ifndef HB_MUSE_PAIRING_H
#define HB_MUSE_PAIRING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define HB_MUSE_PAIRING_JSON_MAX 8192u
/* Pinned SDK accepts at most 4096 unpadded base64url ciphertext bytes =>
 * 3072 plaintext bytes. Two 1023-byte bearer tokens + bounded metadata fit. */
#define HB_MUSE_PAIRING_TOKEN_CAP 1024u
/* Refresh storage/account engine use the upstream 2048-byte token buffer.
 * The complete encrypted command remains subject to the SDK wire bound. */
#define HB_MUSE_PAIRING_REFRESH_CAP 2048u
#define HB_MUSE_PAIRING_WINDOW_MS   120000u
#define HB_MUSE_PAIRING_NETWORK_MAX 16u
/* Dual-band baseline measured 11.6s without BLE. Allow coexistence margin;
 * the original setup window and asynchronous cancellation still apply. */
#define HB_MUSE_PAIRING_SCAN_TIMEOUT_MS 30000u
typedef struct {
  char ssid[33];
  int16_t rssi;
  bool secure;
} hb_muse_pairing_network_t;
typedef enum {
  HB_MUSE_PAIRING_OFF,
  HB_MUSE_PAIRING_OPEN,
  HB_MUSE_PAIRING_HELLO,
  HB_MUSE_PAIRING_CONFIRM,
  HB_MUSE_PAIRING_CONFIRMED,
  HB_MUSE_PAIRING_STORED,
  HB_MUSE_PAIRING_FAILED
} hb_muse_pairing_phase_t;
typedef struct {
  char ssid[33], password[65];
  char access_token[HB_MUSE_PAIRING_TOKEN_CAP], refresh_token[HB_MUSE_PAIRING_REFRESH_CAP];
  char username[128];
  /* Empty selects pinned defaults. No arbitrary endpoint/OTA is accepted. */
  char noise_host[64];
} hb_muse_pairing_credentials_t;
typedef struct {
  /* Fresh monotonic time, also sampled inside store/send guards. */
  uint64_t (*now_ms)(void *user);
  /* Nonblocking facts; must become false immediately on link loss/revoke,
   * including while a synchronous callback is running. No credential logs. */
  bool (*current)(void *user, uint64_t window, uint64_t connection);
  /* Ordered complete JSON notification, no retained pointer. Transport must
   * recheck current and SDK record generation before EACH BLE fragment.
   * Return true only once the whole message was queued on the same link. */
  bool (*send)(
      void *user, uint64_t connection, const char *json, size_t bytes, uint32_t record_generation);
  /* Optional: atomically persist validated credentials, never an API call.
   * Must first quiesce runtime/account workers and serialize identity/store.
   * Recheck guard INSIDE its commit mutex and after verified durable commit.
   * False leaves adapter FAILED; no auto erase/retry. Pointer is borrowed and
   * wiped immediately on return. Never pass credentials to P4 or log them.
   * No provider means provisioning fails closed; not a simulated pairing. */
  bool (*store)(void *user,
                const hb_muse_pairing_credentials_t *credentials,
                bool (*guard)(void *),
                void *guard_user);
  void *user;
  /* Existing identities, copied. node_id is backend identity, device_id is
   * the pairing transcript identity; do not substitute one for the other. */
  const char *node_id, *device_id, *mac, *firmware_version;
  /* Locally entered maker SDK token, never a chat/API/device token. Copied.
   * Required nonempty mgst_ value; never printed. Not stored by this module. */
  const char *sdk_token;
  /* Optional real Wi-Fi provider: bounded <=5s, read-only survey only, no
   * fabricated/stale result. Set count<=capacity; recheck guard inside the
   * existing radio/config owner before scan and after completion. Entries
   * need complete NUL-terminated printable-ASCII SSIDs; skip unsupported SSID
   * encodings rather than truncate. No cloud/API or automatic connection. */
  bool (*wifi_scan)(void *user,
                    hb_muse_pairing_network_t *out,
                    size_t capacity,
                    size_t *count,
                    bool (*guard)(void *),
                    void *guard_user);
  /* Existing Wi-Fi owner: save/request then wait at most 15s for this SSID's
   * actual association + nonzero IP. Guard before/after each stage. No API.
   * Called before durable account store, never proof of cloud access. */
  bool (*wifi_connect)(void *user,
                       const hb_muse_pairing_credentials_t *credentials,
                       bool (*guard)(void *),
                       void *guard_user);
} hb_muse_pairing_config_t;
typedef struct {
  hb_muse_pairing_phase_t phase;
  uint64_t window, connection, confirmation_ticket;
  uint32_t sdk_generation;
  bool release_seen;
} hb_muse_pairing_status_t;
bool hb_muse_pairing_init(const hb_muse_pairing_config_t *config);
/* window and connection are strictly increasing nonzero local generations;
 * neither may be recycled on disconnect. Existing paired record must not be
 * replaced implicitly: the settings owner authorizes/open only while unpaired. */
bool hb_muse_pairing_open(uint64_t window, uint64_t now_ms);
bool hb_muse_pairing_connected(uint64_t connection, uint64_t now_ms);
bool hb_muse_pairing_receive(uint64_t connection,
                             const uint8_t *json,
                             size_t bytes,
                             uint64_t now_ms);
/* Ticket from current CONFIRM status + real physical P4 button edges only.
 * First observe released AFTER prompt delivery, then a new pressed edge with
 * strictly larger edge_serial. Rejects held/early/stale/replayed confirmation.
 * Caller must not map phone commands, timers or synthetic UI events here. */
bool hb_muse_pairing_button(uint64_t confirmation_ticket,
                            uint64_t edge_serial,
                            bool pressed,
                            uint64_t now_ms);
void hb_muse_pairing_poll(uint64_t now_ms);
void hb_muse_pairing_disconnect(uint64_t connection);
void hb_muse_pairing_revoke(void);
hb_muse_pairing_status_t hb_muse_pairing_status(void);
#ifdef __cplusplus
}
#endif
#endif
