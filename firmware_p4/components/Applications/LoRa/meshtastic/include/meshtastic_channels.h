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

#ifndef MESHTASTIC_CHANNELS_H
#define MESHTASTIC_CHANNELS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define MT_MAX_CHANNELS 8
#define MT_PSK_SIZE     16

/**
 * @brief Channel role.
 *
 * Matches Channel.Role in the official protobuf.
 */
typedef enum {
  MT_CH_DISABLED = 0,
  MT_CH_PRIMARY = 1,
  MT_CH_SECONDARY = 2,
  MT_CH_COUNT
} mt_channel_role_t;

/**
 * @brief Persisted state of a single channel slot.
 */
typedef struct {
  bool is_used;
  mt_channel_role_t role;
  uint8_t psk[MT_PSK_SIZE];
  uint8_t hash;
  char name[32];
} mt_channel_t;

/**
 * @brief Load channels from NVS or install the default PRIMARY on first boot.
 */
void mt_channels_init(void);

/**
 * @brief Read a channel slot by index.
 *
 * @param idx  Slot index in [0, MT_MAX_CHANNELS).
 * @return Pointer to the internal slot, or NULL if idx is out of range.
 */
const mt_channel_t *mt_channel_get(uint8_t idx);

/**
 * @brief Configure a channel slot and persist to NVS.
 *
 * Computes the 8-bit hash as SHA256(psk)[0]. If role is MT_CH_PRIMARY,
 * any other slot currently marked PRIMARY is demoted to SECONDARY.
 *
 * @param idx   Slot index in [0, MT_MAX_CHANNELS).
 * @param name  Optional display name. May be NULL to keep the existing name.
 * @param psk   Optional 16-byte PSK. May be NULL to keep the existing PSK.
 * @param role  New role. MT_CH_DISABLED clears is_used.
 */
void mt_channel_set(uint8_t idx, const char *name, const uint8_t *psk, mt_channel_role_t role);

/**
 * @brief Disable a channel slot and persist to NVS.
 */
void mt_channel_disable(uint8_t idx);

/**
 * @brief Find an enabled channel whose hash matches.
 *
 * @param hash         8-bit channel hash to match.
 * @param[out] out_idx  Slot index on hit. May be NULL.
 * @param[out] out_psk  PSK pointer on hit. May be NULL.
 * @return true if a matching enabled channel was found.
 */
bool mt_channel_lookup_by_hash(uint8_t hash, uint8_t *out_idx, const uint8_t **out_psk);

/**
 * @brief Return the hash of the primary channel.
 */
uint8_t mt_channel_primary_hash(void);

/**
 * @brief Return the PSK of the primary channel.
 */
const uint8_t *mt_channel_primary_psk(void);

#ifdef __cplusplus
}
#endif

#endif // MESHTASTIC_CHANNELS_H
