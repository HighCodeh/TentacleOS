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

#include "nfc_ntag4xx.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#define TYPE_MASK          0x0FU
#define PRODUCT_TYPE       0x04U
#define HW_TYPE            1U
#define HW_SUBTYPE         2U
#define HW_MAJOR           3U
#define HW_MINOR           4U
#define HW_STORAGE         5U
#define HW_PROTOCOL        6U
#define SW_OFFSET          7U
#define VERSION_UID_OFFSET 14U
#define BATCH_OFFSET       21U
#define BATCH_EXTRA        25U
#define WEEK_OFFSET        26U
#define YEAR_OFFSET        27U
#define FAB_OFFSET         28U
#define MODEL_413          0x10U
#define MODEL_424          0x30U
#define TAGTAMPER          0x08U
#define NIBBLE_SHIFT       4U
#define WEEK_MASK          0x7FU
#define FAB_SHIFT          7U
#define UID_SHORT          4U
#define UID_LONG           7U
#define ISO_DEP_SAK        0x20U
#define PAGE_MEMORY        1U
#define PAGE_HARDWARE      2U
#define PAGE_SOFTWARE      3U
#define PAGE_BATCH         4U

static const char *TAG = "NFC_NTAG4XX";

esp_err_t nfc_ntag4xx_parse_version(const uint8_t *data,
                                    size_t length,
                                    uint8_t out_version[NFC_NTAG4XX_VERSION_SIZE]) {
  if (data == NULL || out_version == NULL)
    return ESP_ERR_INVALID_ARG;
  if ((length != NFC_NTAG4XX_VERSION_BASE && length != NFC_NTAG4XX_VERSION_SIZE) ||
      (data[HW_TYPE] & TYPE_MASK) != PRODUCT_TYPE)
    return ESP_ERR_INVALID_RESPONSE;
  memmove(out_version, data, length);
  if (length == NFC_NTAG4XX_VERSION_BASE)
    out_version[FAB_OFFSET] = 0;
  return ESP_OK;
}

esp_err_t nfc_ntag4xx_read(mf_desfire_session_t *session,
                           const nfc_iso14443a_data_t *identity,
                           nfc_ntag4xx_card_t *out_card) {
  if (session == NULL || identity == NULL || out_card == NULL)
    return ESP_ERR_INVALID_ARG;
  if ((identity->uid_len != UID_SHORT && identity->uid_len != UID_LONG) ||
      !(identity->sak & ISO_DEP_SAK) || session->dep.ats_len == 0 ||
      session->dep.ats_len > T4T_EMU_ATS_CAPACITY || session->dep.ats[0] != session->dep.ats_len)
    return ESP_ERR_INVALID_RESPONSE;
  memset(out_card, 0, sizeof(*out_card));
  uint8_t response[NFC_NTAG4XX_VERSION_SIZE + 1];
  size_t length = 0;
  uint16_t status = 0;
  esp_err_t error = mf_desfire_transceive_native(
      session, MF_DESFIRE_CMD_GET_VERSION, NULL, 0, response, sizeof(response), &length, &status);
  if (error != ESP_OK)
    return error;
  if (status != MF_DESFIRE_SW_OK)
    return ESP_ERR_INVALID_RESPONSE;
  error = nfc_ntag4xx_parse_version(response, length, out_card->version);
  if (error != ESP_OK)
    return error;
  out_card->identity.uid_len = identity->uid_len;
  memcpy(out_card->identity.uid, identity->uid, identity->uid_len);
  memcpy(out_card->identity.atqa, identity->atqa, sizeof(identity->atqa));
  out_card->identity.sak = identity->sak;
  out_card->identity.ats_len = session->dep.ats_len;
  memcpy(out_card->identity.ats, session->dep.ats, session->dep.ats_len);
  ESP_LOGD(TAG, "Read %s", nfc_ntag4xx_name(out_card));
  return ESP_OK;
}

const char *nfc_ntag4xx_name(const nfc_ntag4xx_card_t *card) {
  if (card == NULL)
    return "Unknown NTAG4xx";
  if (card->version[HW_MAJOR] == MODEL_413)
    return "NTAG413 DNA";
  if (card->version[HW_MAJOR] == MODEL_424)
    return card->version[HW_SUBTYPE] & TAGTAMPER ? "NTAG424 DNA TagTamper" : "NTAG424 DNA";
  return "Unknown NTAG4xx";
}

void nfc_ntag4xx_page(const nfc_ntag4xx_card_t *card,
                      size_t page,
                      char *out_text,
                      size_t capacity) {
  if (card == NULL || out_text == NULL || capacity == 0)
    return;
  const uint8_t *version = card->version;
  switch (page % NFC_NTAG4XX_PAGES) {
    case PAGE_MEMORY:
      snprintf(out_text,
               capacity,
               "%s\n%s\n%s\nNDEF captured: %u bytes\nIdentity emulation only",
               nfc_ntag4xx_name(card),
               version[HW_MAJOR] == MODEL_413   ? "NDEF capacity: 126 bytes"
               : version[HW_MAJOR] == MODEL_424 ? "NDEF capacity: 254 bytes"
                                                : "Memory size unknown",
               version[HW_MAJOR] == MODEL_424   ? "CC: 32; proprietary: 128"
               : version[HW_MAJOR] == MODEL_413 ? "CC: 32; proprietary: 0"
                                                : "Unknown model retained",
               (unsigned)card->ndef_length);
      break;
    case PAGE_HARDWARE:
    case PAGE_SOFTWARE: {
      const uint8_t *part = version + (page == PAGE_SOFTWARE ? SW_OFFSET : 0);
      snprintf(out_text,
               capacity,
               "%s\nVendor %02X Type %02X Sub %02X\nVersion %02X.%02X\n"
               "Storage code %02X Protocol %02X",
               page == PAGE_SOFTWARE ? "Software" : "Hardware",
               part[0],
               part[HW_TYPE],
               part[HW_SUBTYPE],
               part[HW_MAJOR],
               part[HW_MINOR],
               part[HW_STORAGE],
               part[HW_PROTOCOL]);
      break;
    }
    case PAGE_BATCH:
      snprintf(out_text,
               capacity,
               "Batch %02X%02X%02X%02X%X\nWeek %u Year %02X\nFab key %02X ID %02X\n"
               "Version UID\n%02X%02X%02X%02X%02X%02X%02X",
               version[BATCH_OFFSET],
               version[BATCH_OFFSET + 1],
               version[BATCH_OFFSET + 2],
               version[BATCH_OFFSET + 3],
               version[BATCH_EXTRA] >> NIBBLE_SHIFT,
               version[WEEK_OFFSET] & WEEK_MASK,
               version[YEAR_OFFSET],
               ((version[BATCH_EXTRA] & TYPE_MASK) << 1) | (version[WEEK_OFFSET] >> FAB_SHIFT),
               version[FAB_OFFSET],
               version[VERSION_UID_OFFSET],
               version[VERSION_UID_OFFSET + 1],
               version[VERSION_UID_OFFSET + 2],
               version[VERSION_UID_OFFSET + 3],
               version[VERSION_UID_OFFSET + 4],
               version[VERSION_UID_OFFSET + 5],
               version[VERSION_UID_OFFSET + 6]);
      break;
    default: {
      char uid[T4T_EMU_UID_CAPACITY * 2 + 1] = {0};
      for (size_t i = 0; i < card->identity.uid_len; i++)
        snprintf(uid + i * 2, sizeof(uid) - i * 2, "%02X", card->identity.uid[i]);
      snprintf(out_text,
               capacity,
               "%s\nUID %s\nATQA %02X%02X SAK %02X\nATS: %u bytes\nCC: %s NDEF: %s",
               nfc_ntag4xx_name(card),
               uid,
               card->identity.atqa[1],
               card->identity.atqa[0],
               card->identity.sak,
               card->identity.ats_len,
               card->has_cc ? "read" : "unavailable",
               card->has_ndef ? "read" : "unavailable");
      break;
    }
  }
}
