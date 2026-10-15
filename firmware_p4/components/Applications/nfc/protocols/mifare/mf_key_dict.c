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

#include "mf_key_dict.h"

#include "esp_attr.h"
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "highboy_nfc_error.h"
#include "nfc_dict_loader.h"
#include "tos_flash_paths.h"
#include "tos_storage_paths.h"

static const char *TAG = "NFC_MF_KEY_DICT";

#define MF_KEY_SIZE 6

#define SD_USER_DIC TOS_PATH_NFC_DICT "/mf_classic_user.dic"

#define LEGACY_NVS_NS         "nfc_dict"
#define LEGACY_NVS_KEY_COUNT  "extra_count"
#define LEGACY_NVS_KEY_PREFIX "k_"

EXT_RAM_BSS_ATTR static uint8_t s_keys[MF_KEY_DICT_MAX_KEYS][MF_KEY_SIZE];
EXT_RAM_BSS_ATTR static uint8_t s_merge_keys[MF_KEY_DICT_MAX_KEYS][MF_KEY_SIZE];
static int s_count = 0;

static void migrate_legacy_nvs(void) {
  nvs_handle_t ro;
  if (nvs_open(LEGACY_NVS_NS, NVS_READONLY, &ro) != ESP_OK)
    return;

  uint8_t count = 0;
  esp_err_t err = nvs_get_u8(ro, LEGACY_NVS_KEY_COUNT, &count);
  nvs_close(ro);
  if (err != ESP_OK || count == 0)
    return;

  nvs_handle_t rw;
  if (nvs_open(LEGACY_NVS_NS, NVS_READWRITE, &rw) != ESP_OK)
    return;

  int migrated = 0;
  for (uint8_t i = 0; i < count; i++) {
    char key_name[16];
    snprintf(key_name, sizeof(key_name), LEGACY_NVS_KEY_PREFIX "%u", (unsigned)i);

    uint8_t key[MF_KEY_SIZE];
    size_t len = MF_KEY_SIZE;
    if (nvs_get_blob(rw, key_name, key, &len) != ESP_OK || len != MF_KEY_SIZE)
      continue;

    if (nfc_dict_append_key(SD_USER_DIC, key, MF_KEY_SIZE) == ESP_OK)
      migrated++;
  }

  nvs_erase_all(rw);
  nvs_commit(rw);
  nvs_close(rw);

  ESP_LOGI(TAG, "Migrated %d legacy NVS keys into %s", migrated, SD_USER_DIC);
}

void mf_key_dict_init(void) {
  s_count = 0;
  memset(s_keys, 0, sizeof(s_keys));

  migrate_legacy_nvs();

  /* Always seed from the firmware image so an older first-boot copy on the
   * SD card cannot hide
   * a dictionary added by a firmware update. */
  const nfc_dict_scan_params_t flash_scan = {
      .dir = FLASH_NFC_DICT,
      .prefix = "mf_classic_",
      .key_size = MF_KEY_SIZE,
      .max_keys = MF_KEY_DICT_MAX_KEYS,
  };
  size_t loaded = 0;
  nfc_dict_load_dir(&flash_scan, (uint8_t *)s_keys, &loaded);

  /* Merge every user/extra SD dictionary after the built-ins. Duplicate keys,
   * including an old
   * SD default file, are skipped. */
  memset(s_merge_keys, 0, sizeof(s_merge_keys));
  const nfc_dict_scan_params_t sd_scan = {
      .dir = TOS_PATH_NFC_DICT,
      .prefix = "mf_classic_",
      .key_size = MF_KEY_SIZE,
      .max_keys = MF_KEY_DICT_MAX_KEYS,
  };
  size_t sd_loaded = 0;
  nfc_dict_load_dir(&sd_scan, (uint8_t *)s_merge_keys, &sd_loaded);
  for (size_t i = 0; i < sd_loaded && loaded < MF_KEY_DICT_MAX_KEYS; i++) {
    const uint8_t *key = s_merge_keys[i];
    if (nfc_dict_contains((const uint8_t *)s_keys, loaded, MF_KEY_SIZE, key))
      continue;
    memcpy(s_keys[loaded++], key, MF_KEY_SIZE);
  }
  s_count = (int)loaded;

  if (s_count == 0)
    ESP_LOGW(TAG, "No MIFARE Classic dictionaries found on flash or SD");
  else
    ESP_LOGI(TAG, "Dictionary ready: %d keys (%u SD candidates)", s_count, (unsigned)sd_loaded);
}

int mf_key_dict_count(void) {
  return s_count;
}

void mf_key_dict_get(int idx, uint8_t key_out[MF_KEY_SIZE]) {
  if (idx < 0 || idx >= s_count)
    return;
  memcpy(key_out, s_keys[idx], MF_KEY_SIZE);
}

bool mf_key_dict_contains(const uint8_t key[MF_KEY_SIZE]) {
  return nfc_dict_contains((const uint8_t *)s_keys, (size_t)s_count, MF_KEY_SIZE, key);
}

hb_nfc_err_t mf_key_dict_add(const uint8_t key[MF_KEY_SIZE]) {
  if (mf_key_dict_contains(key))
    return HB_NFC_OK;

  if (s_count >= MF_KEY_DICT_MAX_KEYS) {
    ESP_LOGE(TAG, "Dictionary full (%d), cannot add key", MF_KEY_DICT_MAX_KEYS);
    return HB_NFC_ERR_INTERNAL;
  }

  memcpy(s_keys[s_count], key, MF_KEY_SIZE);
  s_count++;

  if (nfc_dict_append_key(SD_USER_DIC, key, MF_KEY_SIZE) != ESP_OK) {
    ESP_LOGW(TAG, "Could not persist key to %s (in-memory only)", SD_USER_DIC);
    return HB_NFC_ERR_INTERNAL;
  }

  ESP_LOGI(TAG, "Persisted new key to %s (total %d)", SD_USER_DIC, s_count);
  return HB_NFC_OK;
}
