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

#include "nfc_store.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "NFC_STORE";

#define NFC_STORE_NVS_NS       "nfc_vault"
#define NFC_STORE_MAGIC        0x48424E46UL /* HBNF */
#define NFC_STORE_VERSION      1U
#define NFC_STORE_HEADER_SIZE  54U
#define NFC_STORE_MFUL_MAGIC   0x4C55464DUL /* MFUL, little-endian */
#define NFC_STORE_MFUL_VERSION 1U
#define NFC_STORE_MFUL_HDR     15U

static bool s_initialized = false;

static void put_u16_le(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)(value >> 8);
}

static uint16_t get_u16_le(const uint8_t *src) {
  return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static void put_u32_le(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)((value >> 8) & 0xFFU);
  dst[2] = (uint8_t)((value >> 16) & 0xFFU);
  dst[3] = (uint8_t)(value >> 24);
}

static uint32_t get_u32_le(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
         ((uint32_t)src[3] << 24);
}

static void slot_key(int slot, char key[8]) {
  snprintf(key, 8, "c%02d", slot);
}

static nvs_handle_t store_open(nvs_open_mode_t mode) {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(NFC_STORE_NVS_NS, mode, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
    return 0;
  }
  return handle;
}

static bool slot_exists(nvs_handle_t nvs, int slot) {
  char key[8];
  slot_key(slot, key);
  size_t len = 0;
  return nvs_get_blob(nvs, key, NULL, &len) == ESP_OK && len >= NFC_STORE_HEADER_SIZE;
}

static int logical_to_slot(nvs_handle_t nvs, int logical_index) {
  if (logical_index < 0)
    return -1;
  int current = 0;
  for (int slot = 0; slot < NFC_STORE_MAX_ENTRIES; slot++) {
    if (!slot_exists(nvs, slot))
      continue;
    if (current == logical_index)
      return slot;
    current++;
  }
  return -1;
}

static hb_nfc_err_t load_slot(nvs_handle_t nvs, int slot, nfc_store_entry_t *out) {
  if (out == NULL || slot < 0 || slot >= NFC_STORE_MAX_ENTRIES)
    return HB_NFC_ERR_PARAM;

  char key[8];
  slot_key(slot, key);
  size_t len = 0;
  esp_err_t err = nvs_get_blob(nvs, key, NULL, &len);
  if (err == ESP_ERR_NVS_NOT_FOUND)
    return HB_NFC_ERR_NOT_FOUND;
  if (err != ESP_OK || len < NFC_STORE_HEADER_SIZE ||
      len > NFC_STORE_HEADER_SIZE + NFC_STORE_PAYLOAD_MAX)
    return HB_NFC_ERR_INTERNAL;

  uint8_t *record = malloc(len);
  if (record == NULL)
    return ESP_ERR_NO_MEM;
  err = nvs_get_blob(nvs, key, record, &len);
  if (err != ESP_OK) {
    free(record);
    return HB_NFC_ERR_INTERNAL;
  }

  const uint16_t payload_len = get_u16_le(&record[10]);
  const bool valid = get_u32_le(&record[0]) == NFC_STORE_MAGIC && record[4] == NFC_STORE_VERSION &&
                     record[6] <= HIGHBOY_NFC_UID_MAX_LEN && record[5] < HIGHBOY_NFC_PROTO_COUNT &&
                     payload_len <= NFC_STORE_PAYLOAD_MAX &&
                     len == NFC_STORE_HEADER_SIZE + payload_len;
  if (!valid) {
    free(record);
    ESP_LOGW(TAG, "Ignoring malformed NFC vault slot %d", slot);
    return HB_NFC_ERR_PROTOCOL;
  }

  memset(out, 0, sizeof(*out));
  out->protocol = (hb_nfc_protocol_t)record[5];
  out->uid_len = record[6];
  out->sak = record[7];
  memcpy(out->atqa, &record[8], sizeof(out->atqa));
  out->payload_len = payload_len;
  memcpy(out->name, &record[12], NFC_STORE_NAME_MAX);
  out->name[NFC_STORE_NAME_MAX - 1] = '\0';
  memcpy(out->uid, &record[44], HIGHBOY_NFC_UID_MAX_LEN);
  if (payload_len > 0)
    memcpy(out->payload, &record[NFC_STORE_HEADER_SIZE], payload_len);
  free(record);
  return HB_NFC_OK;
}

static bool same_name(nvs_handle_t nvs, int slot, const char *name) {
  nfc_store_entry_t *entry = malloc(sizeof(*entry));
  if (entry == NULL)
    return false;
  bool equal = load_slot(nvs, slot, entry) == HB_NFC_OK && strcmp(entry->name, name) == 0;
  free(entry);
  return equal;
}

void nfc_store_init(void) {
  if (s_initialized)
    return;
  nvs_handle_t nvs = store_open(NVS_READWRITE);
  if (nvs != 0) {
    nvs_close(nvs);
    s_initialized = true;
    ESP_LOGI(TAG, "NFC vault ready (%d entries)", nfc_store_count());
  }
}

int nfc_store_count(void) {
  nvs_handle_t nvs = store_open(NVS_READONLY);
  if (nvs == 0)
    return 0;
  int count = 0;
  for (int slot = 0; slot < NFC_STORE_MAX_ENTRIES; slot++)
    count += slot_exists(nvs, slot) ? 1 : 0;
  nvs_close(nvs);
  return count;
}

hb_nfc_err_t nfc_store_save(const nfc_store_card_t *card) {
  if (card == NULL || card->name == NULL || card->name[0] == '\0' || card->uid == NULL ||
      card->uid_len == 0 || card->uid_len > HIGHBOY_NFC_UID_MAX_LEN ||
      card->protocol >= HIGHBOY_NFC_PROTO_COUNT || card->payload_len > NFC_STORE_PAYLOAD_MAX ||
      (card->payload_len > 0 && card->payload == NULL)) {
    return HB_NFC_ERR_PARAM;
  }

  nvs_handle_t nvs = store_open(NVS_READWRITE);
  if (nvs == 0)
    return HB_NFC_ERR_INTERNAL;

  int slot = -1;
  for (int i = 0; i < NFC_STORE_MAX_ENTRIES; i++) {
    if (slot_exists(nvs, i) && same_name(nvs, i, card->name)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    for (int i = 0; i < NFC_STORE_MAX_ENTRIES; i++) {
      if (!slot_exists(nvs, i)) {
        slot = i;
        break;
      }
    }
  }
  if (slot < 0) {
    nvs_close(nvs);
    return ESP_ERR_NO_MEM;
  }

  const size_t record_len = NFC_STORE_HEADER_SIZE + card->payload_len;
  uint8_t *record = calloc(1, record_len);
  if (record == NULL) {
    nvs_close(nvs);
    return ESP_ERR_NO_MEM;
  }
  put_u32_le(&record[0], NFC_STORE_MAGIC);
  record[4] = NFC_STORE_VERSION;
  record[5] = (uint8_t)card->protocol;
  record[6] = card->uid_len;
  record[7] = card->sak;
  memcpy(&record[8], card->atqa, 2);
  put_u16_le(&record[10], (uint16_t)card->payload_len);
  strlcpy((char *)&record[12], card->name, NFC_STORE_NAME_MAX);
  memcpy(&record[44], card->uid, card->uid_len);
  if (card->payload_len > 0)
    memcpy(&record[NFC_STORE_HEADER_SIZE], card->payload, card->payload_len);

  char key[8];
  slot_key(slot, key);
  esp_err_t err = nvs_set_blob(nvs, key, record, record_len);
  free(record);
  if (err == ESP_OK)
    err = nvs_commit(nvs);
  nvs_close(nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Saving '%s' failed: %s", card->name, esp_err_to_name(err));
    return HB_NFC_ERR_INTERNAL;
  }
  ESP_LOGI(TAG,
           "Saved '%s' in slot %d (%u payload bytes)",
           card->name,
           slot,
           (unsigned)card->payload_len);
  return HB_NFC_OK;
}

hb_nfc_err_t nfc_store_load(int index, nfc_store_entry_t *out) {
  if (out == NULL || index < 0)
    return HB_NFC_ERR_PARAM;
  nvs_handle_t nvs = store_open(NVS_READONLY);
  if (nvs == 0)
    return HB_NFC_ERR_INTERNAL;
  int slot = logical_to_slot(nvs, index);
  hb_nfc_err_t err = slot < 0 ? HB_NFC_ERR_NOT_FOUND : load_slot(nvs, slot, out);
  nvs_close(nvs);
  return err;
}

hb_nfc_err_t nfc_store_get_info(int index, nfc_store_info_t *out) {
  if (out == NULL)
    return HB_NFC_ERR_PARAM;
  nfc_store_entry_t *entry = malloc(sizeof(*entry));
  if (entry == NULL)
    return ESP_ERR_NO_MEM;
  hb_nfc_err_t err = nfc_store_load(index, entry);
  if (err == HB_NFC_OK) {
    memset(out, 0, sizeof(*out));
    strlcpy(out->name, entry->name, sizeof(out->name));
    out->protocol = entry->protocol;
    out->uid_len = entry->uid_len;
    memcpy(out->uid, entry->uid, entry->uid_len);
    memcpy(out->atqa, entry->atqa, sizeof(out->atqa));
    out->sak = entry->sak;
    out->payload_len = entry->payload_len;
  }
  free(entry);
  return err;
}

int nfc_store_list(nfc_store_info_t *infos, int max) {
  if (infos == NULL || max <= 0)
    return 0;
  int count = nfc_store_count();
  if (count > max)
    count = max;
  int listed = 0;
  for (int i = 0; i < count; i++) {
    if (nfc_store_get_info(i, &infos[listed]) == HB_NFC_OK)
      listed++;
  }
  return listed;
}

hb_nfc_err_t nfc_store_delete(int index) {
  if (index < 0)
    return HB_NFC_ERR_PARAM;
  nvs_handle_t nvs = store_open(NVS_READWRITE);
  if (nvs == 0)
    return HB_NFC_ERR_INTERNAL;
  int slot = logical_to_slot(nvs, index);
  if (slot < 0) {
    nvs_close(nvs);
    return HB_NFC_ERR_NOT_FOUND;
  }
  char key[8];
  slot_key(slot, key);
  esp_err_t err = nvs_erase_key(nvs, key);
  if (err == ESP_OK)
    err = nvs_commit(nvs);
  nvs_close(nvs);
  return err == ESP_OK ? HB_NFC_OK : HB_NFC_ERR_INTERNAL;
}

hb_nfc_err_t nfc_store_find_by_uid(const uint8_t *uid, uint8_t len, int *out) {
  if (uid == NULL || len == 0 || len > HIGHBOY_NFC_UID_MAX_LEN || out == NULL)
    return HB_NFC_ERR_PARAM;
  int count = nfc_store_count();
  for (int i = 0; i < count; i++) {
    nfc_store_info_t info;
    if (nfc_store_get_info(i, &info) == HB_NFC_OK && info.uid_len == len &&
        memcmp(info.uid, uid, len) == 0) {
      *out = i;
      return HB_NFC_OK;
    }
  }
  return HB_NFC_ERR_NOT_FOUND;
}

size_t nfc_store_pack_mful(const nfc_mful_pack_t *in, uint8_t *out, size_t max) {
  if (in == NULL || out == NULL || in->version == NULL || in->pages == NULL ||
      in->page_count == 0 || in->page_count > (NFC_STORE_PAYLOAD_MAX / 4U))
    return 0;
  const size_t pages_len = (size_t)in->page_count * 4U;
  const size_t total = NFC_STORE_MFUL_HDR + pages_len;
  if (total > max)
    return 0;
  put_u32_le(&out[0], NFC_STORE_MFUL_MAGIC);
  out[4] = NFC_STORE_MFUL_VERSION;
  put_u16_le(&out[5], in->page_count);
  memcpy(&out[7], in->version, 8);
  memcpy(&out[NFC_STORE_MFUL_HDR], in->pages, pages_len);
  return total;
}

bool nfc_store_unpack_mful(const uint8_t *payload, size_t payload_len, nfc_mful_unpack_t *out) {
  if (payload == NULL || out == NULL || out->version == NULL || out->pages == NULL ||
      out->page_count == NULL || payload_len < NFC_STORE_MFUL_HDR ||
      get_u32_le(&payload[0]) != NFC_STORE_MFUL_MAGIC || payload[4] != NFC_STORE_MFUL_VERSION)
    return false;
  const uint16_t page_count = get_u16_le(&payload[5]);
  const size_t pages_len = (size_t)page_count * 4U;
  if (page_count == 0 || page_count > out->pages_max ||
      payload_len != NFC_STORE_MFUL_HDR + pages_len)
    return false;
  memcpy(out->version, &payload[7], 8);
  memcpy(out->pages, &payload[NFC_STORE_MFUL_HDR], pages_len);
  *out->page_count = page_count;
  return true;
}
