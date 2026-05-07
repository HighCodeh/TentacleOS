// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MESHCORE_INTERNAL_H
#define MESHCORE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

#include "meshcore.h"

#define MC_VERSION_V1      0x00
#define MC_HASH_SIZE_1B    0x00
#define MC_CIPHER_KEY_SIZE 16
#define MC_CIPHER_MAC_SIZE 2
#define MC_CIPHER_BLOCK    16
#define MC_HMAC_KEY_SIZE   32
#define MC_PUB_KEY_SIZE    32
#define MC_SIG_SIZE        64

#define MC_DEDUP_SIZE   128
#define MC_DEDUP_TTL_MS (5 * 60 * 1000)

#define MC_PENDINGS_SIZE  8
#define MC_PENDING_TTL_MS 30000

#define MC_ADVERT_COOLDOWN_MS 10000

#define MC_RELAY_QUEUE_SIZE     4
#define MC_RELAY_DELAY_MIN_MS   120
#define MC_RELAY_DELAY_RANGE_MS 360

#define MC_CAD_TIMEOUT_MS  80
#define MC_CAD_MAX_RETRIES 3

#define MC_FALLBACK_EPOCH 1767225600UL

/**
 * @brief Monotonic time in ms since boot (wraps at ~49 days).
 */
uint32_t mc_now_ms(void);

/**
 * @brief Read-only access to identity loaded in meshcore_init.
 */
const meshcore_identity_t *mc_get_identity(void);

/**
 * @brief Read-only access to callbacks registered in meshcore_init.
 */
const meshcore_callbacks_t *mc_get_callbacks(void);

/**
 * @brief Indicates whether continuous RX is active.
 */
bool mc_is_running(void);

/**
 * @brief Retrieves advert lat/lon for internal use (build_advert).
 */
void mc_get_advert_latlon_internal(int32_t *out_lat, int32_t *out_lon, bool *out_has);

/* Wire format helpers (meshcore_crypto.c) */
uint8_t mc_build_header(uint8_t ver, uint8_t ptype, uint8_t route);
uint8_t mc_build_path_len(uint8_t hash_size_sel, uint8_t count);
uint8_t mc_parse_hash_size(uint8_t sel);

/**
 * @brief sha256(a || b)[:out_len]. Used for ACK CRC.
 */
void mc_sha256_two(
    uint8_t *out, size_t out_len, const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len);

/* DB internal (meshcore_db.c) */
bool mc_dedup_check_add(const uint8_t hash[8]);
void mc_pendings_gc(void);
void mc_pendings_add(uint32_t crc, const uint8_t peer_pub[32]);
bool mc_pendings_match(uint32_t crc, uint8_t out_peer[32]);

/**
 * @brief Initializes dedup, pendings, channels and contacts structures.
 *        Called by meshcore_init.
 */
void mc_db_init(void);

/**
 * @brief Read-only access to the channels array (use meshcore_channel_get
 *        for an individual slot).
 */
const meshcore_channel_t *mc_channels_array(void);

/**
 * @brief Mutable access to the contacts array for internal writes
 *        (process_path_payload, etc).
 */
meshcore_contact_t *mc_contacts_array_mut(void);

/**
 * @brief Looks up a contact and returns a mutable pointer (NULL if not found).
 */
meshcore_contact_t *mc_contact_find_mut(const uint8_t pub_key[32]);

/**
 * @brief Persists the contacts array to NVS.
 */
void mc_contacts_persist(void);

/* Router internal (meshcore_router.c) */

/**
 * @brief Initializes router state (relay queue, flags). Called by meshcore_init.
 */
void mc_router_init(void);

/**
 * @brief Drains the relay TX queue. Called by meshcore_poll.
 */
void mc_router_relay_process(void);

/**
 * @brief Marks the packet currently being processed as locally consumed
 *        (blocks relay for that packet).
 */
void mc_router_set_consumed(bool is_consumed);

/**
 * @brief Transmits raw bytes with anti-collision CAD + retry.
 */
esp_err_t mc_transmit_raw(const uint8_t *raw, uint16_t len);

/**
 * @brief Builds and sends an encrypted PAYLOAD_TYPE_PATH to a peer.
 */
esp_err_t mc_send_path_return(const uint8_t peer_pub_key[32],
                              const uint8_t *path_bytes,
                              uint8_t path_count,
                              uint8_t hash_size,
                              uint8_t extra_type,
                              const uint8_t *extra,
                              size_t extra_len);

/**
 * @brief Updates timestamp of last self-advert TX (for cooldown).
 */
void mc_router_mark_advert_sent(void);

#ifdef __cplusplus
}
#endif

#endif // MESHCORE_INTERNAL_H
