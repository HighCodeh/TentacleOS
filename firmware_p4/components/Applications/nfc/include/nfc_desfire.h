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

#ifndef NFC_DESFIRE_H
#define NFC_DESFIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "t4t_emu.h"
#include "mf_desfire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_DESFIRE_MAX_APPS     128U
#define NFC_DESFIRE_MAX_FILES    32U
#define NFC_DESFIRE_MAX_KEYS     14U
#define NFC_DESFIRE_VERSION_SIZE 28U
#define NFC_DESFIRE_MAX_DATA     131072U
#define NFC_DESFIRE_AID_SIZE     3U
#define NFC_DESFIRE_RIGHTS_SIZE  (NFC_DESFIRE_MAX_KEYS * 2U)

typedef struct {
  bool has_settings;
  uint8_t settings, flags, count;
  uint8_t versions[NFC_DESFIRE_MAX_KEYS];
  bool has_version[NFC_DESFIRE_MAX_KEYS];
} nfc_desfire_keys_t;

typedef struct {
  uint8_t id, type, communication;
  bool has_settings, has_data;
  uint8_t rights[NFC_DESFIRE_RIGHTS_SIZE], rights_length;
  uint32_t size, maximum, current;
  uint32_t low, high, credit, counter_limit;
  bool limited_credit;
  uint8_t key_option, key_version;
  uint8_t *data;
  size_t length;
} nfc_desfire_file_t;

typedef struct {
  uint8_t aid[NFC_DESFIRE_AID_SIZE];
  nfc_desfire_keys_t keys;
  bool has_files;
  size_t file_count;
  nfc_desfire_file_t *files;
} nfc_desfire_app_t;

typedef struct {
  t4t_emu_identity_t identity;
  uint8_t version[NFC_DESFIRE_VERSION_SIZE];
  bool has_free_memory, has_apps;
  uint32_t free_memory;
  nfc_desfire_keys_t keys;
  size_t app_count;
  nfc_desfire_app_t *apps;
} nfc_desfire_card_t;

typedef bool (*nfc_desfire_cancel_cb_t)(void);

/* Outputs must be zero initialized; free before reuse. Partial reads keep absence flags. */
void nfc_desfire_free(nfc_desfire_card_t *card);
esp_err_t nfc_desfire_read(const nfc_iso14443a_data_t *identity,
                           nfc_desfire_card_t *out_card,
                           nfc_desfire_cancel_cb_t cancel);
esp_err_t nfc_desfire_load(const char *path, nfc_desfire_card_t *out_card);
esp_err_t nfc_desfire_save(const char *path, const nfc_desfire_card_t *card);
esp_err_t
nfc_desfire_parse_settings(const uint8_t *data, size_t length, nfc_desfire_file_t *out_file);
size_t nfc_desfire_page_count(const nfc_desfire_card_t *card);
void nfc_desfire_page(const nfc_desfire_card_t *card, size_t page, char *out_text, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
