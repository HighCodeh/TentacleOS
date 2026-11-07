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

#ifndef NFC_PLUS_FILE_H
#define NFC_PLUS_FILE_H

#include "t4t_emu.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_PLUS_FILE_MAX_BLOCKS    256U
#define NFC_PLUS_FILE_MAX_KEYS      80U
#define NFC_PLUS_FILE_EXTRA_COUNT   8U
#define NFC_PLUS_FILE_TYPE_CAPACITY 32U

typedef struct {
  t4t_emu_identity_t identity;
  uint16_t block_count;
  char card_type[NFC_PLUS_FILE_TYPE_CAPACITY];
  uint8_t extra[NFC_PLUS_FILE_EXTRA_COUNT][MFP_BLOCK_SIZE];
  bool extra_present[NFC_PLUS_FILE_EXTRA_COUNT];
  bool has_extra[NFC_PLUS_FILE_EXTRA_COUNT];
  uint8_t version[MFP_VERSION_SIZE];
  uint8_t signature[MFP_SIGNATURE_SIZE];
  bool has_signature;
  uint8_t blocks[NFC_PLUS_FILE_MAX_BLOCKS][MFP_BLOCK_SIZE];
  uint8_t keys[NFC_PLUS_FILE_MAX_KEYS][MFP_BLOCK_SIZE];
  bool has_block[NFC_PLUS_FILE_MAX_BLOCKS];
  bool has_key[NFC_PLUS_FILE_MAX_KEYS];
} nfc_plus_file_t;

/* Imports SL3 ordinary data and sector keys from Flipper v4, data format 1.
 * Unknown blocks/keys remain unavailable. Clears output on any parse failure. */
esp_err_t nfc_plus_file_load(const char *path, nfc_plus_file_t *out_card);
/* Read-only replay: original file and card data are never modified by a reader. */
void nfc_plus_file_listener(nfc_plus_file_t *card, mfp_listener_config_t *out_config);
/* Caller retains exclusive card ownership until listener cleanup completes. */
void nfc_plus_file_listener_mutable(nfc_plus_file_t *card, mfp_listener_config_t *out_config);
/* Serialize only while the radio worker is stopped. */
esp_err_t nfc_plus_file_save(const char *path, const nfc_plus_file_t *card);

#ifdef __cplusplus
}
#endif
#endif
