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

#include "nfc_t2t_file.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define LINE_CAPACITY    256U
#define PAGE_SIZE        4U
#define PAGE_LIMIT       (NFC_T2T_FILE_MAX_BYTES / PAGE_SIZE)
#define REQUIRED_FIELDS  0x1FFU
#define HEX_SHIFT        4U
#define HEX_ALPHA        10U
#define UID_PREFIX_SIZE  3U
#define ULTRALIGHT_PAGES 16U
#define AES_PAGES        60U
#define ULC_PAGES        48U
#define ULC_KEY_PAGE     44U
#define EV11_PAGES       20U
#define EV21_PAGES       41U
#define NTAG213_PAGES    45U
#define NTAG215_PAGES    135U
#define NTAG216_PAGES    231U
#define DECIMAL_BASE     10

static const char *TAG = "NFC_T2T_FILE";

typedef enum {
  FIELD_FILETYPE,
  FIELD_VERSION,
  FIELD_DEVICE,
  FIELD_UID,
  FIELD_ATQA,
  FIELD_SAK,
  FIELD_TOTAL,
  FIELD_READ,
  FIELD_TYPE,
  FIELD_COUNT
} nfc_t2t_field_t;

static char *trim(char *s) {
  while (isspace((unsigned char)*s))
    s++;
  size_t n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1]))
    s[--n] = 0;
  return s;
}

static int digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  c = (char)tolower((unsigned char)c);
  return c >= 'a' && c <= 'f' ? c - 'a' + HEX_ALPHA : -1;
}

static bool bytes(const char *s, uint8_t *out, size_t size, bool *out_known) {
  bool known = true, unknown = false;
  for (size_t i = 0; i < size; i++) {
    while (isspace((unsigned char)*s))
      s++;
    if (!s[0] || !s[1])
      return false;
    if (s[0] == '?' && s[1] == '?') {
      unknown = true;
      out[i] = 0;
    } else {
      int a = digit(s[0]), b = digit(s[1]);
      if (a < 0 || b < 0)
        return false;
      out[i] = (uint8_t)((a << HEX_SHIFT) | b);
      known = false;
    }
    s += 2;
    if (*s && !isspace((unsigned char)*s))
      return false;
  }
  while (isspace((unsigned char)*s))
    s++;
  if (*s || (unknown && !known) || (unknown && out_known == NULL))
    return false;
  if (out_known)
    *out_known = !unknown;
  return true;
}

static bool indexed(const char *text, const char *prefix, size_t limit, size_t *out_index) {
  size_t n = strlen(prefix);
  if (strncmp(text, prefix, n) || !isdigit((unsigned char)text[n]))
    return false;
  char *end;
  unsigned long value = strtoul(text + n, &end, DECIMAL_BASE);
  if (*end || value >= limit)
    return false;
  *out_index = value;
  return true;
}

esp_err_t nfc_t2t_file_load(const char *path, nfc_t2t_file_t *out_card) {
  if (out_card == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out_card, 0, sizeof(*out_card));
  if (path == NULL)
    return ESP_ERR_INVALID_ARG;
  FILE *file = fopen(path, "r");
  if (file == NULL)
    return ESP_ERR_NOT_FOUND;
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  char line[LINE_CAPACITY];
  const char *names[] = {"Filetype",
                         "Version",
                         "Device type",
                         "UID",
                         "ATQA",
                         "SAK",
                         "Pages total",
                         "Pages read",
                         "NTAG/Ultralight type"};
  uint16_t seen = 0;
  bool has_signature = false, has_version = false, has_attempts = false;
  bool has_secrets = false, has_aes_keys = false, has_aes_signature = false;
  bool has_aes_signature_state = false;
  bool pages[PAGE_LIMIT] = {false};
  size_t total = 0, read = 0, expected = 0, page_count = 0;
  while (fgets(line, sizeof(line), file)) {
    if (!strchr(line, '\n') && !feof(file))
      goto cleanup;
    char *name = trim(line);
    if (!*name || *name == '#')
      continue;
    char *sep = strchr(name, ':');
    if (sep == NULL)
      goto cleanup;
    *sep = 0;
    name = trim(name);
    char *value = trim(sep + 1);
    size_t field;
    for (field = 0; field < sizeof(names) / sizeof(names[0]); field++)
      if (!strcmp(name, names[field]))
        break;
    if (field < sizeof(names) / sizeof(names[0])) {
      if (seen & (1U << field))
        goto cleanup;
      seen |= 1U << field;
      switch (field) {
        case FIELD_FILETYPE:
          if (strcmp(value, "Flipper NFC device"))
            goto cleanup;
          break;
        case FIELD_VERSION:
          if (strcmp(value, "4"))
            goto cleanup;
          break;
        case FIELD_DEVICE:
          if (strcmp(value, "NTAG/Ultralight"))
            goto cleanup;
          break;
        case FIELD_UID:
          if (!bytes(value, out_card->config.uid, sizeof(out_card->config.uid), NULL))
            goto cleanup;
          out_card->config.uid_len = sizeof(out_card->config.uid);
          break;
        case FIELD_ATQA:
          if (strcmp(value, "00 44"))
            goto cleanup;
          break;
        case FIELD_SAK:
          if (strcmp(value, "00"))
            goto cleanup;
          break;
        case FIELD_TOTAL:
          if (!indexed(value, "", PAGE_LIMIT + 1, &total))
            goto cleanup;
          break;
        case FIELD_READ:
          if (!indexed(value, "", PAGE_LIMIT + 1, &read))
            goto cleanup;
          break;
        case FIELD_TYPE:
          if (!strcmp(value, "NTAG213")) {
            out_card->config.type = T2T_TAG_NTAG213;
            expected = NTAG213_PAGES;
          } else if (!strcmp(value, "NTAG I2C 1K")) {
            out_card->config.type = T2T_TAG_NTAG_I2C_1K;
            expected = 231U;
          } else if (!strcmp(value, "NTAG I2C 2K")) {
            out_card->config.type = T2T_TAG_NTAG_I2C_2K;
            expected = 485U;
          } else if (!strcmp(value, "NTAG I2C Plus 1K")) {
            out_card->config.type = T2T_TAG_NTAG_I2C_PLUS_1K;
            expected = 236U;
          } else if (!strcmp(value, "NTAG I2C Plus 2K")) {
            out_card->config.type = T2T_TAG_NTAG_I2C_PLUS_2K;
            expected = 492U;
          } else if (!strcmp(value, "NTAG210")) {
            out_card->config.type = T2T_TAG_NTAG210;
            expected = EV11_PAGES;
          } else if (!strcmp(value, "NTAG212")) {
            out_card->config.type = T2T_TAG_NTAG212;
            expected = EV21_PAGES;
          } else if (!strcmp(value, "NTAG215")) {
            out_card->config.type = T2T_TAG_NTAG215;
            expected = NTAG215_PAGES;
          } else if (!strcmp(value, "NTAG216")) {
            out_card->config.type = T2T_TAG_NTAG216;
            expected = NTAG216_PAGES;
          } else if (!strcmp(value, "Mifare Ultralight")) {
            out_card->config.type = T2T_TAG_ULTRALIGHT;
            expected = ULTRALIGHT_PAGES;
          } else if (!strcmp(value, "Mifare Ultralight C")) {
            out_card->config.type = T2T_TAG_ULTRALIGHT_C;
            expected = ULC_PAGES;
          } else if (!strcmp(value, "Mifare Ultralight 11")) {
            out_card->config.type = T2T_TAG_ULTRALIGHT_EV1_11;
            expected = EV11_PAGES;
          } else if (!strcmp(value, "Mifare Ultralight 21")) {
            out_card->config.type = T2T_TAG_ULTRALIGHT_EV1_21;
            expected = EV21_PAGES;
          } else if (!strcmp(value, "Mifare Ultralight AES")) {
            out_card->config.type = T2T_TAG_ULTRALIGHT_AES;
            expected = AES_PAGES;
          } else
            goto cleanup;
          break;
      }
    } else if (!strcmp(name, "Highboy AES known keys")) {
      uint8_t flags[T2T_AES_KEY_COUNT * 2];
      if (has_aes_keys || !bytes(value, flags, sizeof(flags), NULL))
        goto cleanup;
      has_aes_keys = true;
      for (size_t i = 0; i < T2T_AES_KEY_COUNT; i++)
        out_card->config.aes_key_known[i] = flags[i * 2] | ((uint16_t)flags[i * 2 + 1] << 8);
    } else if (!strcmp(name, "Highboy AES signature state")) {
      uint8_t state[3];
      if (has_aes_signature_state || !bytes(value, state, sizeof(state), NULL) ||
          state[2] >= T2T_SIGNATURE_LOCK_COUNT)
        goto cleanup;
      has_aes_signature_state = true;
      out_card->config.aes_signature_known = state[0] | ((uint16_t)state[1] << 8);
      out_card->config.aes_signature_lock = (t2t_signature_lock_t)state[2];
      if (out_card->config.aes_signature_known & ~T2T_AES_SIGNATURE_KNOWN)
        goto cleanup;
    } else if (!strcmp(name, "AES signature")) {
      if (has_aes_signature ||
          !bytes(
              value, out_card->config.aes_signature, sizeof(out_card->config.aes_signature), NULL))
        goto cleanup;
      has_aes_signature = out_card->config.has_aes_signature = true;
    } else if (!strcmp(name, "Highboy known secrets")) {
      uint8_t flags[4];
      if (has_secrets || !bytes(value, flags, sizeof(flags), NULL) || flags[0] > 1 || flags[1] > 1)
        goto cleanup;
      has_secrets = true;
      out_card->config.pwd_known = flags[0];
      out_card->config.pack_known = flags[1];
      out_card->config.ulc_key_known = flags[2] | ((uint16_t)flags[3] << 8);
      out_card->config.enable_pwd = flags[0] && flags[1];
    } else if (!strcmp(name, "Failed authentication attempts")) {
      size_t attempts;
      if (has_attempts || !indexed(value, "", UINT32_MAX, &attempts))
        goto cleanup;
      has_attempts = true;
      out_card->config.failed_auth_attempts = (uint32_t)attempts;
    } else if (!strcmp(name, "Signature")) {
      if (has_signature ||
          !bytes(value, out_card->config.signature, sizeof(out_card->config.signature), NULL))
        goto cleanup;
      has_signature = out_card->config.has_signature = true;
    } else if (!strcmp(name, "Mifare version")) {
      if (has_version ||
          !bytes(value, out_card->config.version, sizeof(out_card->config.version), NULL))
        goto cleanup;
      has_version = out_card->config.has_version = true;
    } else if (!strncmp(name, "Counter ", sizeof("Counter ") - 1)) {
      size_t counter, index;
      if (!indexed(name, "Counter ", T2T_COUNTER_COUNT, &index) ||
          (out_card->config.counter_known & (1U << index)) ||
          !indexed(value, "", T2T_COUNTER_MAX + 1, &counter))
        goto cleanup;
      out_card->config.counter_known |= 1U << index;
      out_card->config.counters[index] = (uint32_t)counter;
      if (index == T2T_COUNTER_COUNT - 1) {
        out_card->config.has_counter = true;
        out_card->config.counter = (uint32_t)counter;
      }
    } else if (!strncmp(name, "Tearing ", sizeof("Tearing ") - 1)) {
      size_t index;
      if (!indexed(name, "Tearing ", T2T_COUNTER_COUNT, &index) ||
          (out_card->config.tearing_known & (1U << index)) ||
          !bytes(value, &out_card->config.tearing[index], 1, NULL))
        goto cleanup;
      out_card->config.tearing_known |= 1U << index;
    } else if (!strncmp(name, "Page ", sizeof("Page ") - 1)) {
      size_t index;
      if (!indexed(name, "Page ", PAGE_LIMIT, &index) || pages[index] ||
          !bytes(value, &out_card->memory[index * PAGE_SIZE], PAGE_SIZE, NULL))
        goto cleanup;
      pages[index] = true;
      page_count++;
    }
  }
  bool is_ulc = out_card->config.type == T2T_TAG_ULTRALIGHT_C;
  if (ferror(file) || seen != REQUIRED_FIELDS || total != expected ||
      (read != total && !(is_ulc && read == ULC_KEY_PAGE)) || page_count != total)
    goto cleanup;
  for (size_t i = 0; i < total; i++)
    if (!pages[i])
      goto cleanup;
  if (memcmp(out_card->memory, out_card->config.uid, UID_PREFIX_SIZE) ||
      memcmp(out_card->memory + PAGE_SIZE, out_card->config.uid + UID_PREFIX_SIZE, PAGE_SIZE))
    goto cleanup;
  if ((out_card->config.type == T2T_TAG_ULTRALIGHT_EV1_11 ||
       out_card->config.type == T2T_TAG_ULTRALIGHT_EV1_21 ||
       out_card->config.type == T2T_TAG_ULTRALIGHT_AES ||
       out_card->config.type >= T2T_TAG_NTAG_I2C_1K) &&
      !has_version)
    goto cleanup;
  if (out_card->config.type >= T2T_TAG_NTAG_I2C_1K &&
      !t2t_i2c_version_matches(out_card->config.type, out_card->config.version))
    goto cleanup;
  if (is_ulc && read == total) {
    memcpy(out_card->config.ulc_key,
           out_card->memory + ULC_KEY_PAGE * PAGE_SIZE,
           sizeof(out_card->config.ulc_key));
    out_card->config.enable_ulc_auth = true;
  }
  if (is_ulc && has_secrets && (out_card->config.ulc_key_known == UINT16_MAX) != (read == total))
    goto cleanup;
  if (is_ulc && read == total)
    out_card->config.ulc_key_known = UINT16_MAX;
  if (out_card->config.type == T2T_TAG_ULTRALIGHT_AES && !has_aes_keys && read == total)
    out_card->config.aes_key_known[0] = UINT16_MAX;
  if (has_aes_signature_state && out_card->config.aes_signature_known && !has_aes_signature)
    goto cleanup;
  if (!has_aes_signature_state && has_aes_signature)
    out_card->config.aes_signature_known = T2T_AES_SIGNATURE_KNOWN;
  out_card->config.has_aes_signature =
      out_card->config.aes_signature_known == T2T_AES_SIGNATURE_KNOWN;
  out_card->length = total * PAGE_SIZE;
  error = ESP_OK;
cleanup:
  fclose(file);
  if (error != ESP_OK) {
    ESP_LOGD(TAG, "Invalid or incomplete NTAG profile: %s", path);
    memset(out_card, 0, sizeof(*out_card));
  }
  return error;
}
