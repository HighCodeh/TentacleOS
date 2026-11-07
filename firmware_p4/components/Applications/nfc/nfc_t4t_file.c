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

#include "nfc_t4t.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "storage_atomic.h"
#include "nfc_iso_file.h"

#define TEXT_CAPACITY   131072U
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
  FIELD_SIZE,
  FIELD_CC,
  FIELD_COUNT
} nfc_t4t_field_t;

static const char *TAG = "NFC_T4T_FILE";
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
                                                     "NDEF Data Size",
                                                     "Highboy CC"};

static char *trim(char *text) {
  while (isspace((unsigned char)*text))
    text++;
  size_t length = strlen(text);
  while (length && isspace((unsigned char)text[length - 1]))
    text[--length] = 0;
  return text;
}

esp_err_t nfc_t4t_load(const char *path, nfc_t4t_card_t *out_card) {
  if (path == NULL || out_card == NULL)
    return ESP_ERR_INVALID_ARG;
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return ESP_ERR_NOT_FOUND;
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  nfc_t4t_card_t *card = NULL;
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
  card = calloc(1, sizeof(*card));
  if (card == NULL) {
    ESP_LOGE(TAG, "Cannot allocate card");
    free(text);
    fclose(file);
    return ESP_ERR_NO_MEM;
  }
  nfc_t4t_defaults(card);
  size_t payload_size = 0;
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
    if (!strcmp(name, "NDEF Data")) {
      size_t chunk = 0;
      if (!nfc_iso_file_hex(trim(separator),
                            card->file + 2 + payload_size,
                            sizeof(card->file) - 2 - payload_size,
                            &chunk) ||
          !chunk || chunk > 16)
        goto cleanup;
      payload_size += chunk;
      continue;
    }
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
      strcmp(values[FIELD_DEVICE], "Type 4 Tag"))
    goto cleanup;
  const char *declared_size = values[FIELD_SIZE];
  if (!*declared_size || strlen(declared_size) > 5 ||
      strspn(declared_size, "0123456789") != strlen(declared_size) ||
      strtoul(declared_size, NULL, 10) != payload_size)
    goto cleanup;
  card->file[0] = payload_size >> 8;
  card->file[1] = payload_size;
  size_t size = 0;
  if (!nfc_iso_file_parse_identity(values + FIELD_UID, &card->identity))
    goto cleanup;
  if (values[FIELD_CC] != NULL) {
    uint8_t cc[10];
    if (!nfc_iso_file_hex(values[FIELD_CC], cc, sizeof(cc), &size) || size != sizeof(cc))
      goto cleanup;
    card->cc.mle = (cc[0] << 8) | cc[1];
    card->cc.mlc = (cc[2] << 8) | cc[3];
    card->cc.ndef_fid = (cc[4] << 8) | cc[5];
    card->cc.ndef_max = (cc[6] << 8) | cc[7];
    card->cc.read_access = cc[8];
    card->cc.write_access = cc[9];
  }
  if (card->cc.ndef_max < payload_size + 2 || card->cc.ndef_max > NFC_T4T_FILE_SIZE ||
      card->cc.mle < 2 || card->cc.mlc < 2 || card->cc.ndef_fid == 0xE103 || !card->cc.ndef_fid)
    goto cleanup;
  *out_card = *card;
  error = ESP_OK;
cleanup:
  free(card);
  free(text);
  fclose(file);
  return error;
}

esp_err_t nfc_t4t_save(const char *path, const nfc_t4t_card_t *card) {
  if (path == NULL || card == NULL)
    return ESP_ERR_INVALID_ARG;
  size_t payload_size = nfc_t4t_length(card);
  if (!nfc_iso_file_valid_identity(&card->identity) || card->cc.ndef_max < payload_size + 2 ||
      card->cc.ndef_max > NFC_T4T_FILE_SIZE || card->cc.mle < 2 || card->cc.mlc < 2 ||
      !card->cc.ndef_fid || card->cc.ndef_fid == 0xE103)
    return ESP_ERR_INVALID_ARG;
  char *text = malloc(TEXT_CAPACITY);
  if (text == NULL) {
    ESP_LOGE(TAG, "Cannot allocate profile output");
    return ESP_ERR_NO_MEM;
  }
  esp_err_t error = ESP_ERR_INVALID_SIZE;
  char *cursor = text;
  size_t remaining = TEXT_CAPACITY;
  int count;
  if (!nfc_iso_file_begin(&cursor, &remaining, "Type 4 Tag", &card->identity))
    goto cleanup;
#define EMIT(key, data, size)                                     \
  do {                                                            \
    if (!nfc_iso_file_emit(&cursor, &remaining, key, data, size)) \
      goto cleanup;                                               \
  } while (0)
  uint8_t cc[] = {card->cc.mle >> 8,
                  card->cc.mle,
                  card->cc.mlc >> 8,
                  card->cc.mlc,
                  card->cc.ndef_fid >> 8,
                  card->cc.ndef_fid,
                  card->cc.ndef_max >> 8,
                  card->cc.ndef_max,
                  card->cc.read_access,
                  card->cc.write_access};
  EMIT("Highboy CC", cc, sizeof(cc));
  count = snprintf(cursor, remaining, "NDEF Data Size: %u\n", (unsigned)payload_size);
  if (count < 0 || (size_t)count >= remaining)
    goto cleanup;
  cursor += count;
  remaining -= count;
  for (size_t offset = 0; offset < payload_size; offset += 16) {
    size_t chunk = payload_size - offset;
    if (chunk > 16)
      chunk = 16;
    EMIT("NDEF Data", card->file + 2 + offset, chunk);
  }
#undef EMIT
  error = storage_write_atomic(path, text, (size_t)(cursor - text));
cleanup:
  free(text);
  return error;
}
