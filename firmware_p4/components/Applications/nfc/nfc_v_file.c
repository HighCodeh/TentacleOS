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

#include "nfc_v_file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "storage_atomic.h"

#define LINE_CAPACITY     (ISO15693_LISTENER_BLOCKS * ISO15693_LISTENER_BLOCK_BYTES * 3U + 64U)
#define LINE_LIMIT        128U
#define EXPORT_CAPACITY   (LINE_CAPACITY + 4096U)
#define HEX_SHIFT         4U
#define HEX_ALPHA         10U
#define UID_BYTES         8U
#define PASSWORD_BYTES    4U
#define REQUIRED_FIELDS   ((1ULL << 13U) - 1U)
#define EAS_FIELDS        ((1ULL << 25U) | (1ULL << 26U) | (1ULL << 27U) | (1ULL << 28U))
#define PROTECTION_MASK   0x33U
#define PROTECTION_FIELDS ((1ULL << 21U) | (1ULL << 22U) | (1ULL << 23U) | (1ULL << 24U))

static const char *TAG = "NFC_V_FILE";
static const char *const s_keys[] = {"Filetype",
                                     "Version",
                                     "Device type",
                                     "UID",
                                     "DSFID",
                                     "AFI",
                                     "IC Reference",
                                     "Lock DSFID",
                                     "Lock AFI",
                                     "Block Count",
                                     "Block Size",
                                     "Data Content",
                                     "Security Status",
                                     "Capabilities",
                                     "Password Read",
                                     "Password Write",
                                     "Password Privacy",
                                     "Password Destroy",
                                     "Password EAS",
                                     "Signature",
                                     "Privacy Mode",
                                     "Protection Pointer",
                                     "Protection Condition",
                                     "Lock EAS",
                                     "Lock PPL",
                                     "EAS Enabled",
                                     "EAS ID",
                                     "EAS Protected",
                                     "AFI Protected",
                                     "Password 64",
                                     "Password Locks",
                                     "Highboy Destroyed",
                                     "Highboy EAS Lock"};

static char *trim(char *text) {
  while (isspace((unsigned char)*text))
    text++;
  size_t size = strlen(text);
  while (size && isspace((unsigned char)text[size - 1]))
    text[--size] = 0;
  return text;
}

static int digit(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  value = (char)tolower((unsigned char)value);
  return value >= 'a' && value <= 'f' ? value - 'a' + HEX_ALPHA : -1;
}

static bool hex(const char *text, uint8_t *out, size_t capacity, size_t *out_size) {
  *out_size = 0;
  while (*text) {
    int high = digit(*text++);
    if (high < 0 || !*text)
      return false;
    int low = digit(*text++);
    if (low < 0 || *out_size == capacity)
      return false;
    out[(*out_size)++] = (uint8_t)((high << HEX_SHIFT) | low);
    if (*text && !isspace((unsigned char)*text))
      return false;
    while (isspace((unsigned char)*text))
      text++;
  }
  return *out_size != 0;
}

static bool boolean(const char *text, bool *out) {
  if (!strcmp(text, "true")) {
    *out = true;
    return true;
  }
  if (!strcmp(text, "false")) {
    *out = false;
    return true;
  }
  return false;
}

esp_err_t nfc_v_file_load(const char *path, iso15693_listener_card_t *out_card) {
  if (!path || !out_card)
    return ESP_ERR_INVALID_ARG;
  memset(out_card, 0, sizeof(*out_card));
  FILE *file = fopen(path, "rb");
  if (!file)
    return ESP_ERR_NOT_FOUND;
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  char *line = malloc(LINE_CAPACITY);
  if (!line) {
    ESP_LOGE(TAG, "Cannot allocate NFC-V import buffer");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  uint64_t seen = 0;
  bool eas_lock = false;
  size_t data_size = 0, security_size = 0;
  uint8_t security[ISO15693_LISTENER_BLOCKS];
  unsigned lines = 0;
  while (fgets(line, LINE_CAPACITY, file)) {
    if (++lines > LINE_LIMIT || (!strchr(line, '\n') && !feof(file)))
      goto cleanup;
    char *key = trim(line);
    if (!*key || *key == '#')
      continue;
    char *value = strchr(key, ':');
    if (!value)
      goto cleanup;
    *value++ = 0;
    key = trim(key);
    value = trim(value);
    size_t field = 0;
    while (field < sizeof(s_keys) / sizeof(s_keys[0]) && strcmp(key, s_keys[field]))
      field++;
    if (field == sizeof(s_keys) / sizeof(s_keys[0]) || (seen & (1ULL << field)))
      goto cleanup;
    seen |= 1ULL << field;
    size_t bytes = 0;
    uint8_t byte = 0;
    bool valid = false;
    switch (field) {
      case 0:
        valid = !strcmp(value, "Flipper NFC device");
        break;
      case 1:
        valid = !strcmp(value, "4");
        break;
      case 2:
        out_card->is_slix = !strcmp(value, "SLIX");
        valid = out_card->is_slix || !strcmp(value, "ISO15693-3");
        break;
      case 3:
        valid = hex(value, out_card->uid, UID_BYTES, &bytes) && bytes == UID_BYTES;
        break;
      case 4:
      case 5:
      case 6:
      case 10:
      case 21:
      case 22:
        valid = hex(value, &byte, 1, &bytes) && bytes == 1;
        if (field == 4)
          out_card->dsfid = byte;
        else if (field == 5)
          out_card->afi = byte;
        else if (field == 6)
          out_card->ic_ref = byte;
        else if (field == 10)
          out_card->block_size = byte;
        else if (field == 21)
          out_card->protection_pointer = byte;
        else
          out_card->protection_condition = byte;
        break;
      case 7:
        valid = boolean(value, &out_card->dsfid_locked);
        break;
      case 8:
        valid = boolean(value, &out_card->afi_locked);
        break;
      case 9: {
        unsigned blocks = 0;
        valid = *value != 0;
        for (const char *p = value; *p; p++) {
          if (*p < '0' || *p > '9' || blocks > ISO15693_LISTENER_BLOCKS) {
            valid = false;
            break;
          }
          blocks = blocks * HEX_ALPHA + (unsigned)(*p - '0');
        }
        valid = valid && blocks && blocks <= ISO15693_LISTENER_BLOCKS;
        out_card->block_count = (uint16_t)blocks;
        break;
      }
      case 11:
        valid = hex(value, out_card->memory, sizeof(out_card->memory), &data_size);
        break;
      case 12:
        valid = hex(value, security, sizeof(security), &security_size);
        break;
      case 13:
        valid = !strcmp(value, "Default");
        break;
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
        valid = hex(value, out_card->passwords[field - 14], PASSWORD_BYTES, &bytes) &&
                bytes == PASSWORD_BYTES;
        out_card->has_password[field - 14] = valid;
        break;
      case 19:
        valid = hex(value, out_card->signature, sizeof(out_card->signature), &bytes) &&
                bytes == sizeof(out_card->signature);
        out_card->has_signature = valid;
        break;
      case 20:
        valid = boolean(value, &out_card->is_private);
        break;
      case 23:
        valid = boolean(value, &out_card->eas_locked);
        break;
      case 24:
        valid = boolean(value, &out_card->protection_locked);
        break;
      case 25:
        valid = boolean(value, &out_card->is_eas_enabled);
        break;
      case 26:
        valid = hex(value, out_card->eas_id, sizeof(out_card->eas_id), &bytes) &&
                bytes == sizeof(out_card->eas_id);
        break;
      case 27:
        valid = boolean(value, &out_card->is_eas_protected);
        break;
      case 28:
        valid = boolean(value, &out_card->is_afi_protected);
        break;
      case 29:
        valid = boolean(value, &out_card->is_password_64);
        break;
      case 30: {
        uint8_t locks[ISO15693_LISTENER_PASSWORDS];
        valid = hex(value, locks, sizeof(locks), &bytes) && bytes == sizeof(locks);
        if (valid) {
          for (size_t i = 0; i < sizeof(locks); i++) {
            if (locks[i] > 1)
              valid = false;
            out_card->password_locked[i] = locks[i] != 0;
          }
        }
        break;
      }
      case 31:
        valid = boolean(value, &out_card->is_destroyed);
        break;
      case 32:
        valid = boolean(value, &eas_lock);
        break;
      default:
        break;
    }
    if (!valid)
      goto cleanup;
  }
  if (ferror(file) || (seen & REQUIRED_FIELDS) != REQUIRED_FIELDS || !out_card->block_size ||
      out_card->block_size > ISO15693_LISTENER_BLOCK_BYTES ||
      data_size != (size_t)out_card->block_count * out_card->block_size ||
      security_size != out_card->block_count || out_card->uid[0] != 0xE0U ||
      (out_card->is_slix && out_card->uid[1] != 0x04U) ||
      (out_card->protection_condition & ~PROTECTION_MASK))
    goto cleanup;
  if ((seen & PROTECTION_FIELDS) && (seen & PROTECTION_FIELDS) != PROTECTION_FIELDS)
    goto cleanup;
  if ((seen & EAS_FIELDS) && (seen & EAS_FIELDS) != EAS_FIELDS)
    goto cleanup;
  if (seen & (1ULL << 32U)) {
    if ((seen & PROTECTION_FIELDS) && eas_lock != out_card->eas_locked)
      goto cleanup;
    out_card->eas_locked = eas_lock;
  }
  out_card->has_eas = (seen & EAS_FIELDS) == EAS_FIELDS;
  if (!out_card->is_slix && (seen & ~REQUIRED_FIELDS))
    goto cleanup;
  out_card->has_system_info = (seen & PROTECTION_FIELDS) == PROTECTION_FIELDS;
  if (out_card->has_system_info && out_card->protection_pointer > out_card->block_count)
    goto cleanup;
  for (size_t i = 0; i < security_size; i++) {
    if (security[i] > 1)
      goto cleanup;
    out_card->locked[i] = security[i] != 0;
  }
  for (size_t i = 0; i < UID_BYTES / 2; i++) {
    uint8_t temp = out_card->uid[i];
    out_card->uid[i] = out_card->uid[UID_BYTES - 1 - i];
    out_card->uid[UID_BYTES - 1 - i] = temp;
  }
  out_card->is_read_only = true;
  error = ESP_OK;
cleanup:
  if (error != ESP_OK)
    memset(out_card, 0, sizeof(*out_card));
  free(line);
  fclose(file);
  return error;
}

static bool append(char *text, size_t *offset, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int length = vsnprintf(text + *offset, EXPORT_CAPACITY - *offset, format, args);
  va_end(args);
  if (length < 0 || (size_t)length >= EXPORT_CAPACITY - *offset)
    return false;
  *offset += (size_t)length;
  return true;
}

static bool
append_hex(char *text, size_t *offset, const char *key, const uint8_t *data, size_t length) {
  if (!append(text, offset, "%s: ", key))
    return false;
  for (size_t i = 0; i < length; i++)
    if (!append(text, offset, "%02X%s", data[i], i + 1 == length ? "\n" : " "))
      return false;
  return true;
}

esp_err_t nfc_v_file_save(const char *path, const iso15693_listener_card_t *card) {
  if (!path || !card || !card->block_count || card->block_count > ISO15693_LISTENER_BLOCKS ||
      !card->block_size || card->block_size > ISO15693_LISTENER_BLOCK_BYTES ||
      card->uid[UID_BYTES - 1] != 0xE0U || (card->is_slix && card->uid[UID_BYTES - 2] != 0x04U) ||
      (card->protection_condition & ~PROTECTION_MASK) ||
      (card->has_system_info && card->protection_pointer > card->block_count))
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  char *text = malloc(EXPORT_CAPACITY);
  if (!text) {
    ESP_LOGE(TAG, "Cannot allocate NFC-V export buffer");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  size_t offset = 0;
  uint8_t uid[UID_BYTES], security[ISO15693_LISTENER_BLOCKS];
  for (size_t i = 0; i < UID_BYTES; i++)
    uid[i] = card->uid[UID_BYTES - 1 - i];
  for (size_t i = 0; i < card->block_count; i++)
    security[i] = card->locked[i];
  if (!append(text,
              &offset,
              "Filetype: Flipper NFC device\nVersion: 4\nDevice type: %s\n",
              card->is_slix ? "SLIX" : "ISO15693-3") ||
      !append_hex(text, &offset, "UID", uid, sizeof(uid)) ||
      !append(text,
              &offset,
              "DSFID: %02X\nAFI: %02X\nIC Reference: %02X\nLock DSFID: %s\nLock AFI: %s\nBlock "
              "Count: %u\nBlock Size: %02X\n",
              card->dsfid,
              card->afi,
              card->ic_ref,
              card->dsfid_locked ? "true" : "false",
              card->afi_locked ? "true" : "false",
              card->block_count,
              card->block_size) ||
      !append_hex(text,
                  &offset,
                  "Data Content",
                  card->memory,
                  (size_t)card->block_count * card->block_size) ||
      !append_hex(text, &offset, "Security Status", security, card->block_count))
    goto cleanup;
  if (card->is_slix) {
    static const char *keys[ISO15693_LISTENER_PASSWORDS] = {
        "Password Read", "Password Write", "Password Privacy", "Password Destroy", "Password EAS"};
    uint8_t locks[ISO15693_LISTENER_PASSWORDS];
    for (size_t i = 0; i < ISO15693_LISTENER_PASSWORDS; i++) {
      locks[i] = card->password_locked[i];
      if (card->has_password[i] &&
          !append_hex(text, &offset, keys[i], card->passwords[i], PASSWORD_BYTES))
        goto cleanup;
    }
    if (!append_hex(text, &offset, "Password Locks", locks, sizeof(locks)) ||
        !append(text,
                &offset,
                "Privacy Mode: %s\nPassword 64: %s\nHighboy Destroyed: %s\nHighboy EAS Lock: %s\n",
                card->is_private ? "true" : "false",
                card->is_password_64 ? "true" : "false",
                card->is_destroyed ? "true" : "false",
                card->eas_locked ? "true" : "false"))
      goto cleanup;
    if (card->has_signature &&
        !append_hex(text, &offset, "Signature", card->signature, sizeof(card->signature)))
      goto cleanup;
    if (card->has_system_info &&
        !append(
            text,
            &offset,
            "Protection Pointer: %02X\nProtection Condition: %02X\nLock EAS: %s\nLock PPL: %s\n",
            card->protection_pointer,
            card->protection_condition,
            card->eas_locked ? "true" : "false",
            card->protection_locked ? "true" : "false"))
      goto cleanup;
    if (card->has_eas && (!append(text,
                                  &offset,
                                  "EAS Enabled: %s\nEAS Protected: %s\nAFI Protected: %s\n",
                                  card->is_eas_enabled ? "true" : "false",
                                  card->is_eas_protected ? "true" : "false",
                                  card->is_afi_protected ? "true" : "false") ||
                          !append_hex(text, &offset, "EAS ID", card->eas_id, sizeof(card->eas_id))))
      goto cleanup;
  }
  error = storage_write_atomic(path, text, offset);
cleanup:
  free(text);
  return error;
}
