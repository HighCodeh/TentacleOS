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

#include "mf_key_cache.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "highboy_nfc_error.h"

static const char *TAG = "NFC_MF_KEY_CACHE";

#define NVS_NS              "nfc_kcache"
#define NVS_KEY_COUNT       "count"
#define NVS_ENTRY_PFX       "e_"
#define NVS_ENTRY_NAME_SIZE 16
#define MF_KEY_SIZE         6

static mf_key_cache_entry_t s_cache[MF_KEY_CACHE_MAX_CARDS];
static int s_count = 0;

static int find_entry(const uint8_t *uid, uint8_t uid_len) {
  for (int i = 0; i < s_count; i++) {
    if (s_cache[i].uid_len == uid_len && memcmp(s_cache[i].uid, uid, uid_len) == 0) {
      return i;
    }
  }
  return -1;
}

void mf_key_cache_init(void) {
  s_count = 0;
  memset(s_cache, 0, sizeof(s_cache));

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "NVS open failed (0x%x), starting with empty cache", err);
    return;
  }

  uint8_t count = 0;
  err = nvs_get_u8(handle, NVS_KEY_COUNT, &count);
  if (err != ESP_OK || count == 0) {
    nvs_close(handle);
    return;
  }

  if (count > MF_KEY_CACHE_MAX_CARDS) {
    count = MF_KEY_CACHE_MAX_CARDS;
  }

  for (uint8_t i = 0; i < count; i++) {
    char entry_name[NVS_ENTRY_NAME_SIZE];
    snprintf(entry_name, sizeof(entry_name), NVS_ENTRY_PFX "%u", (unsigned)i);

    size_t len = sizeof(mf_key_cache_entry_t);
    err = nvs_get_blob(handle, entry_name, &s_cache[s_count], &len);
    if (err == ESP_OK && len == sizeof(mf_key_cache_entry_t)) {
      s_count++;
    } else {
      ESP_LOGW(TAG, "Failed to load cache entry %s (err 0x%x)", entry_name, err);
    }
  }

  nvs_close(handle);
  ESP_LOGI(TAG, "Loaded %d cache entries from NVS", s_count);
}

bool mf_key_cache_lookup(
    const uint8_t *uid, uint8_t uid_len, int sector, mf_key_type_t type, uint8_t key_out[6]) {
  if (uid == NULL || sector < 0 || sector >= MF_KEY_CACHE_MAX_SECTORS) {
    return false;
  }

  int idx = find_entry(uid, uid_len);
  if (idx < 0) {
    return false;
  }

  const mf_key_cache_entry_t *entry = &s_cache[idx];

  if (type == MF_KEY_A) {
    if (entry->key_a_known[sector] == NULL) {
      return false;
    }
    memcpy(key_out, entry->key_a[sector], MF_KEY_SIZE);
    return true;
  } else {
    if (entry->key_b_known[sector] == NULL) {
      return false;
    }
    memcpy(key_out, entry->key_b[sector], MF_KEY_SIZE);
    return true;
  }
}

void mf_key_cache_store(const mf_key_cache_store_params_t *params) {
  if (params == NULL || params->uid == NULL || params->key == NULL || params->sector < 0 ||
      params->sector >= MF_KEY_CACHE_MAX_SECTORS) {
    return;
  }

  int idx = find_entry(params->uid, params->uid_len);

  if (idx < 0) {
    if (s_count >= MF_KEY_CACHE_MAX_CARDS) {
      ESP_LOGW(TAG, "Cache full, evicting oldest entry");
      memmove(
          &s_cache[0], &s_cache[1], (MF_KEY_CACHE_MAX_CARDS - 1) * sizeof(mf_key_cache_entry_t));
      s_count--;
    }

    idx = s_count;
    memset(&s_cache[idx], 0, sizeof(mf_key_cache_entry_t));
    memcpy(s_cache[idx].uid, params->uid, params->uid_len);
    s_cache[idx].uid_len = params->uid_len;
    s_cache[idx].sector_count = params->sector_count;
    s_count++;
  } else if (params->sector_count > s_cache[idx].sector_count) {
    s_cache[idx].sector_count = params->sector_count;
  }

  if (params->type == MF_KEY_A) {
    memcpy(s_cache[idx].key_a[params->sector], params->key, MF_KEY_SIZE);
    s_cache[idx].key_a_known[params->sector] = true;
  } else {
    memcpy(s_cache[idx].key_b[params->sector], params->key, MF_KEY_SIZE);
    s_cache[idx].key_b_known[params->sector] = true;
  }
}

void mf_key_cache_save(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS open for save failed (0x%x)", err);
    return;
  }

  for (int i = 0; i < s_count; i++) {
    char entry_name[NVS_ENTRY_NAME_SIZE];
    snprintf(entry_name, sizeof(entry_name), NVS_ENTRY_PFX "%d", i);

    err = nvs_set_blob(handle, entry_name, &s_cache[i], sizeof(mf_key_cache_entry_t));
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "NVS set_blob for entry %d failed (0x%x)", i, err);
    }
  }

  err = nvs_set_u8(handle, NVS_KEY_COUNT, (uint8_t)s_count);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS set count failed (0x%x)", err);
  }

  nvs_commit(handle);
  nvs_close(handle);

  ESP_LOGI(TAG, "Saved %d cache entries to NVS", s_count);
}

void mf_key_cache_clear_uid(const uint8_t *uid, uint8_t uid_len) {
  if (uid == NULL) {
    return;
  }

  int idx = find_entry(uid, uid_len);
  if (idx < 0) {
    return;
  }

  if (idx < s_count - 1) {
    memmove(&s_cache[idx], &s_cache[idx + 1], (s_count - idx - 1) * sizeof(mf_key_cache_entry_t));
  }
  memset(&s_cache[s_count - 1], 0, sizeof(mf_key_cache_entry_t));
  s_count--;

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS open for clear failed (0x%x)", err);
    return;
  }

  for (int i = 0; i < s_count; i++) {
    char entry_name[NVS_ENTRY_NAME_SIZE];
    snprintf(entry_name, sizeof(entry_name), NVS_ENTRY_PFX "%d", i);
    err = nvs_set_blob(handle, entry_name, &s_cache[i], sizeof(mf_key_cache_entry_t));
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "NVS set_blob during clear rewrite failed (0x%x)", err);
    }
  }

  err = nvs_set_u8(handle, NVS_KEY_COUNT, (uint8_t)s_count);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS set count after clear failed (0x%x)", err);
  }

  nvs_commit(handle);
  nvs_close(handle);

  ESP_LOGI(TAG, "Cleared UID entry from cache (remaining: %d)", s_count);
}

int mf_key_cache_get_known_count(const uint8_t *uid, uint8_t uid_len) {
  if (uid == NULL) {
    return 0;
  }

  int idx = find_entry(uid, uid_len);
  if (idx < 0) {
    return 0;
  }

  const mf_key_cache_entry_t *entry = &s_cache[idx];
  int known = 0;
  int sectors = entry->sector_count;
  if (sectors > MF_KEY_CACHE_MAX_SECTORS) {
    sectors = MF_KEY_CACHE_MAX_SECTORS;
  }

  for (int s = 0; s < sectors; s++) {
    if (entry->key_a_known[s])
      known++;
    if (entry->key_b_known[s])
      known++;
  }

  return known;
}
