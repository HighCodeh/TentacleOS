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

#include "meshcore_internal.h"

#include "esp_attr.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"

#include "meshcore_nvs.h"

static const char *TAG = "MC_DB";

#define MC_NVS_KEY_CHANNELS "channels"
#define MC_NVS_KEY_CONTACTS "contacts"

static const uint8_t MC_PUBLIC_CHANNEL_SECRET[16] = {
    0x8b,
    0x33,
    0x87,
    0xe9,
    0xc5,
    0xcd,
    0xea,
    0x6a,
    0xc9,
    0xe5,
    0xed,
    0xba,
    0xa1,
    0x15,
    0xcd,
    0x72,
};

typedef struct {
  bool is_valid;
  uint8_t hash[8];
  uint32_t timestamp_ms;
} mc_dedup_entry_t;

typedef struct {
  bool is_used;
  uint32_t expected_crc;
  uint8_t peer_pub[32];
  uint32_t deadline_ms;
} mc_pending_t;

static meshcore_channel_t s_channels[MESHCORE_MAX_CHANNELS];
EXT_RAM_BSS_ATTR static meshcore_contact_t s_contacts[MESHCORE_MAX_CONTACTS];

static mc_dedup_entry_t s_dedup[MC_DEDUP_SIZE];
static uint16_t s_dedup_idx = 0;

static mc_pending_t s_pendings[MC_PENDINGS_SIZE];

static void channel_compute_hash(meshcore_channel_t *ch);
static void channels_load_or_default(void);
static void contacts_load(void);
static void channels_persist(void);
static meshcore_contact_t *contact_alloc_slot(const uint8_t pub_key[32]);

void mc_db_init(void) {
  memset(s_dedup, 0, sizeof(s_dedup));
  s_dedup_idx = 0;
  memset(s_pendings, 0, sizeof(s_pendings));
  channels_load_or_default();
  contacts_load();
}

const meshcore_channel_t *meshcore_channel_get(uint8_t idx) {
  if (idx >= MESHCORE_MAX_CHANNELS)
    return NULL;
  if (!s_channels[idx].is_used)
    return NULL;
  return &s_channels[idx];
}

bool meshcore_channel_set(uint8_t idx, const char *name, const uint8_t secret[16]) {
  if (idx >= MESHCORE_MAX_CHANNELS)
    return false;

  bool is_empty = (secret == NULL);
  if (!is_empty) {
    bool is_all_zero = true;
    for (int i = 0; i < 16; i++) {
      if (secret[i] != 0) {
        is_all_zero = false;
        break;
      }
    }
    is_empty = is_all_zero;
  }

  if (is_empty) {
    memset(&s_channels[idx], 0, sizeof(meshcore_channel_t));
  } else {
    s_channels[idx].is_used = true;
    memcpy(s_channels[idx].secret, secret, 16);
    if (name == NULL) {
      s_channels[idx].name[0] = 0;
    } else {
      strncpy(s_channels[idx].name, name, MESHCORE_NAME_MAX - 1);
      s_channels[idx].name[MESHCORE_NAME_MAX - 1] = 0;
    }
    channel_compute_hash(&s_channels[idx]);
  }

  channels_persist();
  ESP_LOGI(TAG,
           "Channel %u %s (hash=0x%02X)",
           idx,
           is_empty ? "removed" : "saved",
           is_empty ? 0 : s_channels[idx].hash);
  return true;
}

const meshcore_channel_t *mc_channels_array(void) {
  return s_channels;
}

const meshcore_contact_t *meshcore_contacts_array(void) {
  return s_contacts;
}

meshcore_contact_t *mc_contacts_array_mut(void) {
  return s_contacts;
}

size_t meshcore_contacts_count(void) {
  size_t n = 0;
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (s_contacts[i].is_used)
      n++;
  }
  return n;
}

const meshcore_contact_t *meshcore_contact_find(const uint8_t pub_key[32]) {
  return mc_contact_find_mut(pub_key);
}

const meshcore_contact_t *meshcore_contact_find_by_hash(uint8_t hash_byte) {
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (s_contacts[i].is_used && s_contacts[i].pub_key[0] == hash_byte) {
      return &s_contacts[i];
    }
  }
  return NULL;
}

meshcore_contact_t *mc_contact_find_mut(const uint8_t pub_key[32]) {
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (s_contacts[i].is_used && memcmp(s_contacts[i].pub_key, pub_key, 32) == 0) {
      return &s_contacts[i];
    }
  }
  return NULL;
}

bool meshcore_contact_upsert(const meshcore_contact_t *src) {
  if (src == NULL)
    return false;
  meshcore_contact_t *slot = contact_alloc_slot(src->pub_key);
  if (slot == NULL)
    return false;

  bool is_keep_path = (src->out_path_len == MESHCORE_OUT_PATH_UNKNOWN && slot->is_used &&
                       slot->out_path_len != MESHCORE_OUT_PATH_UNKNOWN);
  uint8_t old_path[MESHCORE_MAX_PATH];
  uint8_t old_path_len = 0;
  if (is_keep_path) {
    old_path_len = slot->out_path_len;
    memcpy(old_path, slot->out_path, MESHCORE_MAX_PATH);
  }
  *slot = *src;
  slot->is_used = true;
  if (is_keep_path) {
    slot->out_path_len = old_path_len;
    memcpy(slot->out_path, old_path, MESHCORE_MAX_PATH);
  }
  if (slot->lastmod == 0)
    slot->lastmod = meshcore_get_unix_time();
  mc_contacts_persist();
  return true;
}

bool meshcore_contact_remove(const uint8_t pub_key[32]) {
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (s_contacts[i].is_used && memcmp(s_contacts[i].pub_key, pub_key, 32) == 0) {
      memset(&s_contacts[i], 0, sizeof(meshcore_contact_t));
      mc_contacts_persist();
      return true;
    }
  }
  return false;
}

bool meshcore_contact_reset_path(const uint8_t pub_key[32]) {
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (s_contacts[i].is_used && memcmp(s_contacts[i].pub_key, pub_key, 32) == 0) {
      s_contacts[i].out_path_len = MESHCORE_OUT_PATH_UNKNOWN;
      memset(s_contacts[i].out_path, 0, MESHCORE_MAX_PATH);
      s_contacts[i].lastmod = meshcore_get_unix_time();
      mc_contacts_persist();
      return true;
    }
  }
  return false;
}

void mc_contacts_persist(void) {
  mc_nvs_set_blob(MC_NVS_KEY_CONTACTS, s_contacts, sizeof(s_contacts));
}

bool mc_dedup_check_add(const uint8_t hash[8]) {
  uint32_t t = mc_now_ms();
  for (int i = 0; i < MC_DEDUP_SIZE; i++) {
    if (s_dedup[i].is_valid && memcmp(s_dedup[i].hash, hash, 8) == 0 &&
        (t - s_dedup[i].timestamp_ms) < MC_DEDUP_TTL_MS) {
      return true;
    }
  }
  memcpy(s_dedup[s_dedup_idx].hash, hash, 8);
  s_dedup[s_dedup_idx].timestamp_ms = t;
  s_dedup[s_dedup_idx].is_valid = true;
  s_dedup_idx = (s_dedup_idx + 1) % MC_DEDUP_SIZE;
  return false;
}

void mc_pendings_gc(void) {
  uint32_t t = mc_now_ms();
  for (int i = 0; i < MC_PENDINGS_SIZE; i++) {
    if (s_pendings[i].is_used && (int32_t)(t - s_pendings[i].deadline_ms) >= 0) {
      ESP_LOGW(TAG,
               "ACK pending #%d expired (CRC=0x%08lX)",
               i,
               (unsigned long)s_pendings[i].expected_crc);
      s_pendings[i].is_used = false;
    }
  }
}

void mc_pendings_add(uint32_t crc, const uint8_t peer_pub[32]) {
  mc_pendings_gc();
  for (int i = 0; i < MC_PENDINGS_SIZE; i++) {
    if (!s_pendings[i].is_used) {
      s_pendings[i].is_used = true;
      s_pendings[i].expected_crc = crc;
      memcpy(s_pendings[i].peer_pub, peer_pub, 32);
      s_pendings[i].deadline_ms = mc_now_ms() + MC_PENDING_TTL_MS;
      return;
    }
  }
  ESP_LOGW(TAG, "Pendings full -- replacing slot 0");
  s_pendings[0].expected_crc = crc;
  memcpy(s_pendings[0].peer_pub, peer_pub, 32);
  s_pendings[0].deadline_ms = mc_now_ms() + MC_PENDING_TTL_MS;
}

bool mc_pendings_match(uint32_t crc, uint8_t out_peer[32]) {
  mc_pendings_gc();
  for (int i = 0; i < MC_PENDINGS_SIZE; i++) {
    if (s_pendings[i].is_used && s_pendings[i].expected_crc == crc) {
      memcpy(out_peer, s_pendings[i].peer_pub, 32);
      s_pendings[i].is_used = false;
      return true;
    }
  }
  return false;
}

static void channel_compute_hash(meshcore_channel_t *ch) {
  uint8_t h[32];
  meshcore_crypto_sha256(ch->secret, 16, h);
  ch->hash = h[0];
}

static void channels_persist(void) {
  mc_nvs_set_blob(MC_NVS_KEY_CHANNELS, s_channels, sizeof(s_channels));
}

static void channels_load_or_default(void) {
  size_t len = sizeof(s_channels);
  esp_err_t err = mc_nvs_get_blob(MC_NVS_KEY_CHANNELS, s_channels, &len);
  if (err == ESP_OK && len == sizeof(s_channels)) {
    ESP_LOGI(TAG, "Channels loaded from NVS");
    return;
  }
  memset(s_channels, 0, sizeof(s_channels));
  s_channels[0].is_used = true;
  memcpy(s_channels[0].secret, MC_PUBLIC_CHANNEL_SECRET, 16);
  strcpy(s_channels[0].name, "Public");
  channel_compute_hash(&s_channels[0]);
  channels_persist();
  ESP_LOGI(TAG, "Channels: Public default in slot 0 (hash=0x%02X)", s_channels[0].hash);
}

static void contacts_load(void) {
  size_t len = sizeof(s_contacts);
  esp_err_t err = mc_nvs_get_blob(MC_NVS_KEY_CONTACTS, s_contacts, &len);
  if (err != ESP_OK || len != sizeof(s_contacts)) {
    memset(s_contacts, 0, sizeof(s_contacts));
    return;
  }
  int n = 0;
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (s_contacts[i].is_used)
      n++;
  }
  ESP_LOGI(TAG, "Contacts loaded from NVS: %d entries", n);
}

static meshcore_contact_t *contact_alloc_slot(const uint8_t pub_key[32]) {
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (s_contacts[i].is_used && memcmp(s_contacts[i].pub_key, pub_key, 32) == 0) {
      return &s_contacts[i];
    }
  }
  for (int i = 0; i < MESHCORE_MAX_CONTACTS; i++) {
    if (!s_contacts[i].is_used)
      return &s_contacts[i];
  }
  int oldest = 0;
  for (int i = 1; i < MESHCORE_MAX_CONTACTS; i++) {
    if (s_contacts[i].lastmod < s_contacts[oldest].lastmod)
      oldest = i;
  }
  return &s_contacts[oldest];
}
