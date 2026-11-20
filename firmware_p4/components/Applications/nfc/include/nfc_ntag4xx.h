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

#ifndef NFC_NTAG4XX_H
#define NFC_NTAG4XX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "t4t_emu.h"
#include "mf_desfire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_NTAG4XX_VERSION_SIZE 29U
#define NFC_NTAG4XX_VERSION_BASE 28U
#define NFC_NTAG4XX_CC_SIZE      12U
#define NFC_NTAG4XX_NDEF_SIZE    1024U
#define NFC_NTAG4XX_PAGES        5U

typedef struct {
  t4t_emu_identity_t identity;
  uint8_t version[NFC_NTAG4XX_VERSION_SIZE];
  bool has_cc;
  bool has_ndef;
  uint8_t cc[NFC_NTAG4XX_CC_SIZE];
  uint8_t ndef[NFC_NTAG4XX_NDEF_SIZE];
  size_t ndef_length;
} nfc_ntag4xx_card_t;

/* Caller owns an activated ISO-DEP session; optional NDEF capture is separate. */
esp_err_t nfc_ntag4xx_read(mf_desfire_session_t *session,
                           const nfc_iso14443a_data_t *identity,
                           nfc_ntag4xx_card_t *out_card);
esp_err_t nfc_ntag4xx_parse_version(const uint8_t *data,
                                    size_t length,
                                    uint8_t out_version[NFC_NTAG4XX_VERSION_SIZE]);
const char *nfc_ntag4xx_name(const nfc_ntag4xx_card_t *card);
esp_err_t nfc_ntag4xx_load(const char *path, nfc_ntag4xx_card_t *out_card);
esp_err_t nfc_ntag4xx_save(const char *path, const nfc_ntag4xx_card_t *card);
void nfc_ntag4xx_page(const nfc_ntag4xx_card_t *card, size_t page, char *out_text, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
