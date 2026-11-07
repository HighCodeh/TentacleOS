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

#include "nfc_emv.h"

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
#define REQUIRED_FIELDS 0x0FFFF83FU

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
  FIELD_HOLDER,
  FIELD_NAME,
  FIELD_LABEL,
  FIELD_PANLEN,
  FIELD_PAN,
  FIELD_AIDLEN,
  FIELD_AID,
  FIELD_AIP,
  FIELD_COUNTRY,
  FIELD_CURRENCY,
  FIELD_EXPY,
  FIELD_EXPM,
  FIELD_EXPD,
  FIELD_EFFY,
  FIELD_EFFM,
  FIELD_EFFD,
  FIELD_PIN,
  FIELD_KNOWN,
  FIELD_EXTRA,
  FIELD_LOGS,
  FIELD_COUNT
} nfc_emv_field_t;

static const char *TAG = "NFC_EMV_FILE";
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
                                                     "Cardholder name",
                                                     "Application name",
                                                     "Application label",
                                                     "PAN length",
                                                     "PAN",
                                                     "AID length",
                                                     "AID",
                                                     "Application interchange profile",
                                                     "Country code",
                                                     "Currency code",
                                                     "Expiration year",
                                                     "Expiration month",
                                                     "Expiration day",
                                                     "Effective year",
                                                     "Effective month",
                                                     "Effective day",
                                                     "PIN try counter",
                                                     "Highboy lengths",
                                                     "Highboy extras",
                                                     "Highboy logs"};

static char *trim(char *text) {
  while (isspace((unsigned char)*text))
    text++;
  size_t length = strlen(text);
  while (length && isspace((unsigned char)text[length - 1]))
    text[--length] = 0;
  return text;
}

static bool valid_profile(const emv_profile_t *profile) {
  const uint8_t limits[] = {10, 2, 16, 16, 26, 3, 3, 2, 2, 1, 2, 2, 1, 2};
  if (profile->app.aid_len < 5 || profile->app.aid_len > EMV_AID_MAX_LEN ||
      profile->log_count > EMV_LOG_COUNT ||
      (profile->is_complete && !profile->length[EMV_FIELD_PAN]))
    return false;
  for (size_t i = 0; i < EMV_FIELD_COUNT; i++) {
    size_t length = profile->length[i];
    bool is_text = i >= EMV_FIELD_NAME && i <= EMV_FIELD_HOLDER;
    if (length > limits[i] || (!is_text && i != EMV_FIELD_PAN && length && length != limits[i]))
      return false;
    for (size_t j = 0; j < length; j++) {
      uint8_t value = profile->value[i][j];
      if (is_text && (value < ' ' || value > '~'))
        return false;
      if (i == EMV_FIELD_PAN &&
          ((value >> 4) > 9 || ((value & 15U) > 9 && !((value & 15U) == 15U && j + 1 == length))))
        return false;
    }
  }
  if (profile->length[EMV_FIELD_PAN] == 10 && (profile->value[EMV_FIELD_PAN][9] & 15U) != 15U)
    return false;
  const uint8_t log_sizes[] = {2, 6, 2, 2, 3, 3};
  for (size_t i = 0; i < profile->log_count; i++)
    for (size_t j = 0; j < EMV_LOG_FIELDS; j++)
      if (profile->logs[i].length[j] && profile->logs[i].length[j] != log_sizes[j])
        return false;
  return true;
}

static bool number(const char *text, unsigned maximum, unsigned *out) {
  if (!text || !*text || strspn(text, "0123456789") != strlen(text))
    return false;
  unsigned long value = strtoul(text, NULL, 10);
  if (value > maximum)
    return false;
  *out = value;
  return true;
}

esp_err_t nfc_emv_load(const char *path, nfc_emv_card_t *out_card) {
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
    char *name = line;
    while (isspace((unsigned char)*name))
      name++;
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
      if (i >= FIELD_HOLDER && i <= FIELD_LABEL) {
        if (*separator == ' ')
          separator++;
        size_t value_length = strlen(separator);
        if (value_length && separator[value_length - 1] == '\r')
          separator[value_length - 1] = 0;
        values[i] = separator;
      } else
        values[i] = trim(separator);
      break;
    }
  }
  if ((seen & REQUIRED_FIELDS) != REQUIRED_FIELDS ||
      strcmp(values[FIELD_FILETYPE], "Flipper NFC device") || strcmp(values[FIELD_VERSION], "4") ||
      strcmp(values[FIELD_DEVICE], "EMV"))
    goto cleanup;
  nfc_emv_card_t card = {0};
  size_t size = 0;
  if (!nfc_iso_file_parse_identity(values + FIELD_UID, &card.identity))
    goto cleanup;
  emv_profile_t *profile = &card.profile;
  unsigned count;
  if (!number(values[FIELD_PANLEN], 10, &count) ||
      !nfc_iso_file_hex(values[FIELD_PAN], profile->value[EMV_FIELD_PAN], 10, &size) ||
      size != count)
    goto cleanup;
  profile->length[EMV_FIELD_PAN] = size;
  if (!number(values[FIELD_AIDLEN], EMV_AID_MAX_LEN, &count) || count < 5 ||
      !nfc_iso_file_hex(values[FIELD_AID], profile->app.aid, EMV_AID_MAX_LEN, &size) ||
      size != count)
    goto cleanup;
  profile->app.aid_len = size;
  const size_t text_fields[] = {EMV_FIELD_HOLDER, EMV_FIELD_NAME, EMV_FIELD_LABEL};
  const size_t text_max[] = {26, 16, 16};
  for (size_t i = 0; i < 3; i++) {
    size_t length = strlen(values[FIELD_HOLDER + i]);
    if (length > text_max[i])
      goto cleanup;
    memcpy(profile->value[text_fields[i]], values[FIELD_HOLDER + i], length);
    profile->length[text_fields[i]] = length;
  }
  const size_t binary_fields[] = {EMV_FIELD_AIP, EMV_FIELD_COUNTRY, EMV_FIELD_CURRENCY};
  for (size_t i = 0; i < 3; i++) {
    uint8_t value[2];
    if (!nfc_iso_file_hex(values[FIELD_AIP + i], value, 2, &size) || size != 2)
      goto cleanup;
    profile->value[binary_fields[i]][0] = value[i ? 1 : 0];
    profile->value[binary_fields[i]][1] = value[i ? 0 : 1];
    profile->length[binary_fields[i]] = i && !value[0] && !value[1] ? 0 : 2;
  }
  for (size_t i = 0; i < 6; i++) {
    size_t field = i < 3 ? EMV_FIELD_EXPIRY : EMV_FIELD_EFFECTIVE;
    if (!nfc_iso_file_hex(values[FIELD_EXPY + i], &profile->value[field][i % 3], 1, &size) ||
        size != 1)
      goto cleanup;
    if (profile->value[field][i % 3])
      profile->length[field] = 3;
  }
  if (!number(values[FIELD_PIN], 255, &count))
    goto cleanup;
  profile->value[EMV_FIELD_PIN_TRIES][0] = count;
  profile->length[EMV_FIELD_PIN_TRIES] = 1;
  profile->is_complete = profile->length[EMV_FIELD_PAN] != 0;
  if (values[FIELD_EXTRA]) {
    uint8_t extra[12];
    if (!nfc_iso_file_hex(values[FIELD_EXTRA], extra, sizeof(extra), &size) ||
        size != sizeof(extra) || extra[11] > 1)
      goto cleanup;
    memcpy(profile->value[EMV_FIELD_ATC], extra, 2);
    memcpy(profile->value[EMV_FIELD_LAST_ATC], extra + 2, 2);
    profile->value[EMV_FIELD_PRIORITY][0] = extra[4];
    memcpy(profile->value[EMV_FIELD_LOG_ENTRY], extra + 5, 2);
    profile->records_read = (extra[7] << 8) | extra[8];
    profile->status = (extra[9] << 8) | extra[10];
    profile->is_complete = extra[11];
  }
  if (values[FIELD_KNOWN]) {
    uint8_t lengths[EMV_FIELD_COUNT];
    const uint8_t limits[] = {10, 2, 16, 16, 26, 3, 3, 2, 2, 1, 2, 2, 1, 2};
    if (!nfc_iso_file_hex(values[FIELD_KNOWN], lengths, sizeof(lengths), &size) ||
        size != sizeof(lengths))
      goto cleanup;
    for (size_t i = 0; i < EMV_FIELD_COUNT; i++) {
      if (lengths[i] > limits[i] ||
          (i != EMV_FIELD_PAN && (i < EMV_FIELD_NAME || i > EMV_FIELD_HOLDER) && lengths[i] &&
           lengths[i] != limits[i]))
        goto cleanup;
      if (i >= EMV_FIELD_ATC && lengths[i] && !values[FIELD_EXTRA])
        goto cleanup;
      if (i <= EMV_FIELD_HOLDER && i != EMV_FIELD_AIP && lengths[i] != profile->length[i])
        goto cleanup;
    }
    memcpy(profile->length, lengths, sizeof(lengths));
    for (size_t i = 0; i < EMV_FIELD_COUNT; i++)
      if (!lengths[i])
        memset(profile->value[i], 0, EMV_FIELD_BYTES);
  }
  if (values[FIELD_LOGS]) {
    uint8_t raw[EMV_LOG_COUNT * 42];
    if (!nfc_iso_file_hex(values[FIELD_LOGS], raw, sizeof(raw), &size) || size % 42)
      goto cleanup;
    profile->log_count = size / 42;
    for (size_t i = 0; i < profile->log_count; i++) {
      static const uint8_t sizes[] = {2, 6, 2, 2, 3, 3};
      memcpy(profile->logs[i].length, raw + i * 42, 6);
      memcpy(profile->logs[i].value, raw + i * 42 + 6, 36);
      for (size_t j = 0; j < 6; j++)
        if (profile->logs[i].length[j] && profile->logs[i].length[j] != sizes[j])
          goto cleanup;
    }
  }
  if (!valid_profile(profile))
    goto cleanup;
  memcpy(profile->app.label, profile->value[EMV_FIELD_LABEL], profile->length[EMV_FIELD_LABEL]);
  *out_card = card;
  error = ESP_OK;
cleanup:
  free(text);
  fclose(file);
  return error;
}

esp_err_t nfc_emv_save(const char *path, const nfc_emv_card_t *card) {
  if (path == NULL || card == NULL)
    return ESP_ERR_INVALID_ARG;
  const emv_profile_t *profile = &card->profile;
  if (!nfc_iso_file_valid_identity(&card->identity) || !valid_profile(profile))
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
  if (!nfc_iso_file_begin(&cursor, &remaining, "EMV", &card->identity))
    goto cleanup;
#define EMIT(key, data, size)                                     \
  do {                                                            \
    if (!nfc_iso_file_emit(&cursor, &remaining, key, data, size)) \
      goto cleanup;                                               \
  } while (0)
  const size_t text_fields[] = {EMV_FIELD_HOLDER, EMV_FIELD_NAME, EMV_FIELD_LABEL};
  for (size_t i = 0; i < 3; i++) {
    size_t field = text_fields[i];
    count = snprintf(cursor,
                     remaining,
                     "%s: %.*s\n",
                     FIELD_NAMES[FIELD_HOLDER + i],
                     profile->length[field],
                     profile->value[field]);
    if (count < 0 || (size_t)count >= remaining)
      goto cleanup;
    cursor += count;
    remaining -= count;
  }
  count = snprintf(cursor, remaining, "PAN length: %u\n", profile->length[EMV_FIELD_PAN]);
  if (count < 0 || (size_t)count >= remaining)
    goto cleanup;
  cursor += count;
  remaining -= count;
  EMIT("PAN", profile->value[EMV_FIELD_PAN], profile->length[EMV_FIELD_PAN]);
  count = snprintf(cursor, remaining, "AID length: %u\n", profile->app.aid_len);
  if (count < 0 || (size_t)count >= remaining)
    goto cleanup;
  cursor += count;
  remaining -= count;
  EMIT("AID", profile->app.aid, profile->app.aid_len);
  EMIT("Application interchange profile", profile->value[EMV_FIELD_AIP], 2);
  uint8_t country[] = {profile->value[EMV_FIELD_COUNTRY][1], profile->value[EMV_FIELD_COUNTRY][0]};
  uint8_t currency[] = {profile->value[EMV_FIELD_CURRENCY][1],
                        profile->value[EMV_FIELD_CURRENCY][0]};
  EMIT("Country code", country, 2);
  EMIT("Currency code", currency, 2);
  for (size_t i = 0; i < 6; i++)
    EMIT(FIELD_NAMES[FIELD_EXPY + i],
         &profile->value[i < 3 ? EMV_FIELD_EXPIRY : EMV_FIELD_EFFECTIVE][i % 3],
         1);
  count =
      snprintf(cursor, remaining, "PIN try counter: %u\n", profile->value[EMV_FIELD_PIN_TRIES][0]);
  if (count < 0 || (size_t)count >= remaining)
    goto cleanup;
  cursor += count;
  remaining -= count;
  EMIT("Highboy lengths", profile->length, EMV_FIELD_COUNT);
  uint8_t extra[] = {profile->value[EMV_FIELD_ATC][0],
                     profile->value[EMV_FIELD_ATC][1],
                     profile->value[EMV_FIELD_LAST_ATC][0],
                     profile->value[EMV_FIELD_LAST_ATC][1],
                     profile->value[EMV_FIELD_PRIORITY][0],
                     profile->value[EMV_FIELD_LOG_ENTRY][0],
                     profile->value[EMV_FIELD_LOG_ENTRY][1],
                     profile->records_read >> 8,
                     profile->records_read,
                     profile->status >> 8,
                     profile->status,
                     profile->is_complete};
  EMIT("Highboy extras", extra, sizeof(extra));
  uint8_t logs[EMV_LOG_COUNT * 42];
  for (size_t i = 0; i < profile->log_count; i++) {
    memcpy(logs + i * 42, profile->logs[i].length, 6);
    memcpy(logs + i * 42 + 6, profile->logs[i].value, 36);
  }
  EMIT("Highboy logs", logs, profile->log_count * 42);
#undef EMIT
  error = storage_write_atomic(path, text, (size_t)(cursor - text));
cleanup:
  free(text);
  return error;
}
