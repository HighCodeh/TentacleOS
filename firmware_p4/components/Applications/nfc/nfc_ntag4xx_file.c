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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "storage_atomic.h"
#include "nfc_iso_file.h"

#define TEXT_CAPACITY   8192U
#define HEX_SHIFT       4U
#define HEX_ALPHA       10U
#define UID_SHORT       4U
#define UID_LONG        7U
#define ISO_DEP_SAK     0x20U
#define ATS_INTERFACE   0x10U
#define ATS_INTERFACES  3U
#define ATS_RESERVED    0x80U
#define ATS_FSCI_MASK   0x0FU
#define ATS_MAX_FSCI    8U
#define REQUIRED_FIELDS 0x083FU

typedef enum {
  FIELD_FILETYPE,
  FIELD_VERSION,
  FIELD_DEVICE,
  FIELD_UID,
  FIELD_ATQA,
  FIELD_SAK,
  FIELD_T0,
  FIELD_TA,
  FIELD_TB,
  FIELD_TC,
  FIELD_HISTORY,
  FIELD_PICC,
  FIELD_CC,
  FIELD_NDEF,
  FIELD_COUNT
} nfc_ntag4xx_field_t;

static const char *TAG = "NFC_NTAG4XX_FILE";
static const char *const FIELD_NAMES[FIELD_COUNT] = {"Filetype",
                                                     "Version",
                                                     "Device type",
                                                     "UID",
                                                     "ATQA",
                                                     "SAK",
                                                     "T0",
                                                     "TA(1)",
                                                     "TB(1)",
                                                     "TC(1)",
                                                     "T1...Tk",
                                                     "PICC Version",
                                                     "Highboy CC",
                                                     "Highboy NDEF"};

static char *trim(char *text) {
  while (isspace((unsigned char)*text))
    text++;
  size_t length = strlen(text);
  while (length && isspace((unsigned char)text[length - 1]))
    text[--length] = 0;
  return text;
}

esp_err_t nfc_ntag4xx_load(const char *path, nfc_ntag4xx_card_t *out_card) {
  if (path == NULL || out_card == NULL)
    return ESP_ERR_INVALID_ARG;
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return ESP_ERR_NOT_FOUND;
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  char *text = malloc(TEXT_CAPACITY + 1);
  if (text == NULL) {
    ESP_LOGE(TAG, "Cannot allocate profile text");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  size_t length = fread(text, 1, TEXT_CAPACITY, file);
  if (ferror(file) || !feof(file) || memchr(text, 0, length) != NULL)
    goto cleanup;
  text[length] = 0;
  for (size_t i = 0; i < length; i++) {
    unsigned char value = (unsigned char)text[i];
    if (value < ' ' && value != '\n' && value != '\r' && value != '\t')
      goto cleanup;
  }
  char *values[FIELD_COUNT] = {0};
  uint32_t seen = 0;
  char *context = NULL;
  for (char *line = strtok_r(text, "\n", &context); line != NULL;
       line = strtok_r(NULL, "\n", &context)) {
    char *name = trim(line);
    if (!*name || *name == '#')
      continue;
    char *separator = strchr(name, ':');
    if (separator == NULL)
      goto cleanup;
    *separator++ = 0;
    name = trim(name);
    for (size_t i = 0; i < FIELD_COUNT; i++) {
      if (strcmp(name, FIELD_NAMES[i]))
        continue;
      if (seen & (1U << i))
        goto cleanup;
      seen |= 1U << i;
      values[i] = trim(separator);
      break;
    }
  }
  if ((seen & REQUIRED_FIELDS) != REQUIRED_FIELDS ||
      strcmp(values[FIELD_FILETYPE], "Flipper NFC device") || strcmp(values[FIELD_VERSION], "4") ||
      strcmp(values[FIELD_DEVICE], "NTAG4xx"))
    goto cleanup;
  nfc_ntag4xx_card_t card = {0};
  size_t size = 0;
  if (!nfc_iso_file_parse_identity(values + FIELD_UID, &card.identity))
    goto cleanup;
  if (!nfc_iso_file_hex(values[FIELD_PICC], card.version, sizeof(card.version), &size) ||
      nfc_ntag4xx_parse_version(card.version, size, card.version) != ESP_OK)
    goto cleanup;
  if (values[FIELD_CC] != NULL) {
    if (!nfc_iso_file_hex(values[FIELD_CC], card.cc, sizeof(card.cc), &size) ||
        size != sizeof(card.cc))
      goto cleanup;
    card.has_cc = true;
  }
  if (values[FIELD_NDEF] != NULL) {
    if (!card.has_cc ||
        !nfc_iso_file_hex(values[FIELD_NDEF], card.ndef, sizeof(card.ndef), &card.ndef_length))
      goto cleanup;
    card.has_ndef = true;
  }
  *out_card = card;
  error = ESP_OK;
cleanup:
  free(text);
  fclose(file);
  return error;
}

esp_err_t nfc_ntag4xx_save(const char *path, const nfc_ntag4xx_card_t *card) {
  if (path == NULL || card == NULL)
    return ESP_ERR_INVALID_ARG;
  uint8_t version[NFC_NTAG4XX_VERSION_SIZE];
  if (!nfc_iso_file_valid_identity(&card->identity) ||
      nfc_ntag4xx_parse_version(card->version, sizeof(card->version), version) != ESP_OK ||
      card->ndef_length > sizeof(card->ndef) || (card->has_ndef && !card->has_cc) ||
      (!card->has_ndef && card->ndef_length))
    return ESP_ERR_INVALID_ARG;
  char *text = malloc(TEXT_CAPACITY);
  if (text == NULL) {
    ESP_LOGE(TAG, "Cannot allocate profile output");
    return ESP_ERR_NO_MEM;
  }
  esp_err_t error = ESP_ERR_INVALID_SIZE;
  char *cursor = text;
  size_t remaining = TEXT_CAPACITY;
  if (!nfc_iso_file_begin(&cursor, &remaining, "NTAG4xx", &card->identity))
    goto cleanup;
#define EMIT(key, data, size)                                     \
  do {                                                            \
    if (!nfc_iso_file_emit(&cursor, &remaining, key, data, size)) \
      goto cleanup;                                               \
  } while (0)
  EMIT("PICC Version", card->version, sizeof(card->version));
  if (card->has_cc)
    EMIT("Highboy CC", card->cc, sizeof(card->cc));
  if (card->has_ndef)
    EMIT("Highboy NDEF", card->ndef, card->ndef_length);
#undef EMIT
  error = storage_write_atomic(path, text, (size_t)(cursor - text));
cleanup:
  free(text);
  return error;
}
