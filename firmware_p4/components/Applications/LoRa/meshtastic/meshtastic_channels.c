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

#include "meshtastic_channels.h"

#include <string.h>

#include "esp_log.h"
#include "mbedtls/sha256.h"

#include "meshtastic_nvs.h"

static const char *TAG = "MESHTASTIC_CHANNELS";

#define MT_CHANNELS_NVS_KEY "channels"

static const uint8_t DEFAULT_PSK[MT_PSK_SIZE] = {
    0xd4,
    0xf1,
    0xbb,
    0x3a,
    0x20,
    0x29,
    0x07,
    0x59,
    0xf0,
    0xbc,
    0xff,
    0xab,
    0xcf,
    0x4e,
    0x69,
    0x01,
};

static mt_channel_t s_channels[MT_MAX_CHANNELS];

static uint8_t compute_hash(const uint8_t *psk) {
  uint8_t digest[32];
  mbedtls_sha256(psk, MT_PSK_SIZE, digest, 0);
  return digest[0];
}

static void persist(void) {
  mt_nvs_set_blob(MT_CHANNELS_NVS_KEY, s_channels, sizeof(s_channels));
}

void mt_channels_init(void) {
  memset(s_channels, 0, sizeof(s_channels));

  int read_len = mt_nvs_get_blob(MT_CHANNELS_NVS_KEY, s_channels, sizeof(s_channels));
  if (read_len != (int)sizeof(s_channels)) {
    ESP_LOGW(TAG, "Channels NVS absent — installing default PRIMARY");
    memset(s_channels, 0, sizeof(s_channels));
    s_channels[0].is_used = true;
    s_channels[0].role = MT_CH_PRIMARY;
    memcpy(s_channels[0].psk, DEFAULT_PSK, MT_PSK_SIZE);
    s_channels[0].hash = compute_hash(DEFAULT_PSK);
    strcpy(s_channels[0].name, "LongFast");
    persist();
  }

  int active_count = 0;
  for (int i = 0; i < MT_MAX_CHANNELS; i++) {
    if (s_channels[i].is_used && s_channels[i].role != MT_CH_DISABLED) {
      active_count++;
    }
  }
  ESP_LOGI(
      TAG, "Initialized — %d active, primary hash=0x%02X", active_count, mt_channel_primary_hash());
}

const mt_channel_t *mt_channel_get(uint8_t idx) {
  if (idx >= MT_MAX_CHANNELS)
    return NULL;
  return &s_channels[idx];
}

void mt_channel_set(uint8_t idx, const char *name, const uint8_t *psk, mt_channel_role_t role) {
  if (idx >= MT_MAX_CHANNELS)
    return;

  s_channels[idx].is_used = (role != MT_CH_DISABLED);
  s_channels[idx].role = role;

  if (psk != NULL) {
    memcpy(s_channels[idx].psk, psk, MT_PSK_SIZE);
    s_channels[idx].hash = compute_hash(psk);
  }
  if (name != NULL) {
    strncpy(s_channels[idx].name, name, sizeof(s_channels[idx].name) - 1);
    s_channels[idx].name[sizeof(s_channels[idx].name) - 1] = 0;
  }

  if (role == MT_CH_PRIMARY) {
    for (int i = 0; i < MT_MAX_CHANNELS; i++) {
      if (i != idx && s_channels[i].role == MT_CH_PRIMARY) {
        s_channels[i].role = MT_CH_SECONDARY;
      }
    }
  }

  persist();
  ESP_LOGI(TAG,
           "Channel %u set — name='%s', role=%d, hash=0x%02X",
           idx,
           s_channels[idx].name,
           (int)s_channels[idx].role,
           s_channels[idx].hash);
}

void mt_channel_disable(uint8_t idx) {
  if (idx >= MT_MAX_CHANNELS)
    return;
  s_channels[idx].is_used = false;
  s_channels[idx].role = MT_CH_DISABLED;
  persist();
}

bool mt_channel_lookup_by_hash(uint8_t hash, uint8_t *out_idx, const uint8_t **out_psk) {
  for (int i = 0; i < MT_MAX_CHANNELS; i++) {
    if (s_channels[i].is_used && s_channels[i].role != MT_CH_DISABLED &&
        s_channels[i].hash == hash) {
      if (out_idx != NULL)
        *out_idx = (uint8_t)i;
      if (out_psk != NULL)
        *out_psk = s_channels[i].psk;
      return true;
    }
  }
  return false;
}

uint8_t mt_channel_primary_hash(void) {
  for (int i = 0; i < MT_MAX_CHANNELS; i++) {
    if (s_channels[i].is_used && s_channels[i].role == MT_CH_PRIMARY) {
      return s_channels[i].hash;
    }
  }
  return 0;
}

const uint8_t *mt_channel_primary_psk(void) {
  for (int i = 0; i < MT_MAX_CHANNELS; i++) {
    if (s_channels[i].is_used && s_channels[i].role == MT_CH_PRIMARY) {
      return s_channels[i].psk;
    }
  }
  return DEFAULT_PSK;
}
