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

#include "nfc_desfire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define CMD_FREE_MEMORY        0x6EU
#define CMD_KEY_VERSION        0x64U
#define CMD_FILE_SETTINGS      0xF5U
#define CMD_VALUE              0x6CU
#define CMD_RECORDS            0xBBU
#define STATUS_DENIED          0x919DU
#define STATUS_AUTH            0x91AEU
#define STATUS_ILLEGAL         0x911CU
#define FILE_DATA              0U
#define FILE_BACKUP            1U
#define FILE_VALUE             2U
#define FILE_LINEAR            3U
#define FILE_CYCLIC            4U
#define FILE_TMAC              5U
#define WORD_SIZE              4U
#define BYTE_BITS              8U
#define NIBBLE_BITS            4U
#define NIBBLE_MASK            0x0FU
#define FREE_ACCESS            0x0EU
#define RIGHTS_READ_SHIFT      12U
#define SETTINGS_HEADER        4U
#define SETTINGS_VALUE_SIZE    17U
#define SETTINGS_RECORD_SIZE   13U
#define SETTINGS_TMAC_SIZE     6U
#define EXTRA_RIGHTS           0x80U
#define COUNTER_PRESENT        0x20U
#define AES_COUNTER            0x02U
#define SETTINGS_BUFFER        64U
#define READ_CHUNK             512U
#define TYPE_MASK              0x0FU
#define TYPE_DESFIRE           0x01U
#define TYPE_LIGHT             0x08U
#define VERSION_TYPE_OFFSET    1U
#define VERSION_HW_MAJOR       3U
#define VERSION_SW_MAJOR       10U
#define VIEW_BYTES             16U
#define VIEW_ROW_BYTES         8U
#define VIEW_KEYS              4U
#define COMMUNICATION_MASK     0x03U
#define COMMUNICATION_RESERVED 0x02U
#define UID_SHORT_SIZE         4U
#define UID_LONG_SIZE          7U

static const char *TAG = "NFC_DESFIRE";

static uint32_t little(const uint8_t *data, size_t length) {
  uint32_t value = 0;
  for (size_t i = 0; i < length; i++)
    value |= (uint32_t)data[i] << (BYTE_BITS * i);
  return value;
}

void nfc_desfire_free(nfc_desfire_card_t *card) {
  if (!card)
    return;
  for (size_t a = 0; a < card->app_count; a++) {
    nfc_desfire_app_t *app = &card->apps[a];
    for (size_t f = 0; f < app->file_count; f++)
      free(app->files[f].data);
    free(app->files);
  }
  free(card->apps);
  memset(card, 0, sizeof(*card));
}

esp_err_t
nfc_desfire_parse_settings(const uint8_t *data, size_t length, nfc_desfire_file_t *out_file) {
  if (!data || !out_file)
    return ESP_ERR_INVALID_ARG;
  if (length < SETTINGS_TMAC_SIZE)
    return ESP_ERR_INVALID_RESPONSE;
  nfc_desfire_file_t file = {.id = out_file->id};
  file.type = data[0];
  file.communication = data[1] & COMMUNICATION_MASK;
  if (file.communication == COMMUNICATION_RESERVED)
    return ESP_ERR_INVALID_RESPONSE;
  memcpy(file.rights, data + 2, 2);
  file.rights_length = 2;
  size_t consumed;
  if (file.type == FILE_DATA || file.type == FILE_BACKUP) {
    consumed = SETTINGS_HEADER + NFC_DESFIRE_AID_SIZE;
    if (length < consumed)
      return ESP_ERR_INVALID_RESPONSE;
    file.size = little(data + SETTINGS_HEADER, NFC_DESFIRE_AID_SIZE);
  } else if (file.type == FILE_VALUE) {
    consumed = SETTINGS_VALUE_SIZE;
    if (length < consumed || data[consumed - 1] > 1)
      return ESP_ERR_INVALID_RESPONSE;
    file.low = little(data + SETTINGS_HEADER, WORD_SIZE);
    file.high = little(data + SETTINGS_HEADER + WORD_SIZE, WORD_SIZE);
    file.credit = little(data + SETTINGS_HEADER + WORD_SIZE * 2, WORD_SIZE);
    file.limited_credit = data[consumed - 1];
    file.size = WORD_SIZE;
  } else if (file.type == FILE_LINEAR || file.type == FILE_CYCLIC) {
    consumed = SETTINGS_RECORD_SIZE;
    if (length < consumed)
      return ESP_ERR_INVALID_RESPONSE;
    file.size = little(data + SETTINGS_HEADER, NFC_DESFIRE_AID_SIZE);
    file.maximum = little(data + SETTINGS_HEADER + NFC_DESFIRE_AID_SIZE, NFC_DESFIRE_AID_SIZE);
    file.current = little(data + SETTINGS_HEADER + NFC_DESFIRE_AID_SIZE * 2, NFC_DESFIRE_AID_SIZE);
    if (!file.size || file.current > file.maximum)
      return ESP_ERR_INVALID_RESPONSE;
  } else if (file.type == FILE_TMAC) {
    consumed = SETTINGS_TMAC_SIZE;
    file.key_option = data[SETTINGS_HEADER];
    file.key_version = data[SETTINGS_HEADER + 1];
    if (data[1] & COUNTER_PRESENT) {
      size_t bytes = file.key_option & AES_COUNTER ? WORD_SIZE : 2;
      if (length < consumed + bytes)
        return ESP_ERR_INVALID_RESPONSE;
      for (size_t i = 0; i < bytes; i++)
        file.counter_limit = (file.counter_limit << BYTE_BITS) | data[consumed++];
    }
  } else
    return ESP_ERR_NOT_SUPPORTED;
  if (data[1] & EXTRA_RIGHTS) {
    if (length <= consumed)
      return ESP_ERR_INVALID_RESPONSE;
    size_t count = data[consumed++];
    if (count >= NFC_DESFIRE_MAX_KEYS || length - consumed != count * 2)
      return ESP_ERR_INVALID_RESPONSE;
    memcpy(file.rights + 2, data + consumed, count * 2);
    file.rights_length += count * 2;
    consumed += count * 2;
  }
  if (length != consumed)
    return ESP_ERR_INVALID_RESPONSE;
  file.has_settings = true;
  *out_file = file;
  return ESP_OK;
}

static esp_err_t command(mf_desfire_session_t *session,
                         uint8_t cmd,
                         const uint8_t *params,
                         size_t params_length,
                         uint8_t *out,
                         size_t capacity,
                         size_t *out_length,
                         bool optional) {
  uint16_t status = 0;
  esp_err_t error = mf_desfire_transceive_native(
      session, cmd, params, params_length, out, capacity, out_length, &status);
  if (error != ESP_OK)
    return error;
  if (status == MF_DESFIRE_SW_OK)
    return ESP_OK;
  if (optional && (status == STATUS_DENIED || status == STATUS_AUTH || status == STATUS_ILLEGAL))
    return ESP_ERR_NOT_FOUND;
  return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t
read_keys(mf_desfire_session_t *session, nfc_desfire_keys_t *keys, nfc_desfire_cancel_cb_t cancel) {
  uint8_t response[2];
  size_t length = 0;
  esp_err_t error = command(
      session, MF_DESFIRE_CMD_GET_KEY_SET, NULL, 0, response, sizeof(response), &length, true);
  if (error == ESP_ERR_NOT_FOUND)
    return ESP_OK;
  if (error != ESP_OK)
    return error;
  if (length != sizeof(response))
    return ESP_ERR_INVALID_RESPONSE;
  keys->settings = response[0];
  keys->flags = response[1] >> NIBBLE_BITS;
  keys->count = response[1] & NIBBLE_MASK;
  if (keys->count > NFC_DESFIRE_MAX_KEYS)
    return ESP_ERR_INVALID_RESPONSE;
  keys->has_settings = true;
  for (uint8_t i = 0; i < keys->count; i++) {
    if (cancel && cancel())
      return ESP_ERR_INVALID_STATE;
    error = command(session, CMD_KEY_VERSION, &i, 1, response, sizeof(response), &length, true);
    if (error == ESP_ERR_NOT_FOUND)
      continue;
    if (error != ESP_OK)
      return error;
    if (length != 1)
      return ESP_ERR_INVALID_RESPONSE;
    keys->versions[i] = response[0];
    keys->has_version[i] = true;
  }
  return ESP_OK;
}

static void put24(uint8_t *out, uint32_t value) {
  for (size_t i = 0; i < NFC_DESFIRE_AID_SIZE; i++)
    out[i] = value >> (BYTE_BITS * i);
}

static esp_err_t read_contents(mf_desfire_session_t *session,
                               nfc_desfire_file_t *file,
                               size_t *total,
                               nfc_desfire_cancel_cb_t cancel) {
  bool can_read = false;
  for (size_t i = 0; i < file->rights_length; i += 2) {
    uint32_t rights = little(file->rights + i, 2);
    can_read |= ((rights >> RIGHTS_READ_SHIFT) & NIBBLE_MASK) == FREE_ACCESS ||
                ((rights >> NIBBLE_BITS) & NIBBLE_MASK) == FREE_ACCESS;
  }
  if (!can_read || file->type == FILE_TMAC)
    return ESP_OK;
  size_t bytes = file->size;
  bool is_records = file->type == FILE_LINEAR || file->type == FILE_CYCLIC;
  if (is_records) {
    if (file->current && file->size > NFC_DESFIRE_MAX_DATA / file->current)
      return ESP_ERR_INVALID_SIZE;
    bytes *= file->current;
  }
  if (bytes > NFC_DESFIRE_MAX_DATA - *total)
    return ESP_ERR_INVALID_SIZE;
  if (!bytes) {
    file->has_data = true;
    return ESP_OK;
  }
  file->data = malloc(bytes);
  if (!file->data) {
    ESP_LOGE(TAG, "Cannot allocate file contents");
    return ESP_ERR_NO_MEM;
  }
  esp_err_t error = ESP_OK;
  size_t offset = 0;
  while (offset < bytes) {
    if (cancel && cancel()) {
      error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    uint8_t params[1 + NFC_DESFIRE_AID_SIZE * 2] = {file->id};
    uint8_t cmd = file->type == FILE_VALUE ? CMD_VALUE
                  : is_records             ? CMD_RECORDS
                                           : MF_DESFIRE_CMD_READ_DATA;
    size_t chunk = is_records ? file->size : bytes - offset;
    if (!is_records && chunk > READ_CHUNK)
      chunk = READ_CHUNK;
    put24(params + 1, is_records ? offset / file->size : offset);
    put24(params + 1 + NFC_DESFIRE_AID_SIZE, is_records ? 1 : chunk);
    size_t received = 0;
    error = command(session,
                    cmd,
                    params,
                    file->type == FILE_VALUE ? 1 : sizeof(params),
                    file->data + offset,
                    chunk,
                    &received,
                    true);
    if (error != ESP_OK)
      goto cleanup;
    if (received != chunk) {
      error = ESP_ERR_INVALID_RESPONSE;
      goto cleanup;
    }
    offset += received;
  }
  file->length = bytes;
  file->has_data = true;
  *total += bytes;
  return ESP_OK;
cleanup:
  free(file->data);
  file->data = NULL;
  return error == ESP_ERR_NOT_FOUND ? ESP_OK : error;
}

esp_err_t nfc_desfire_read(const nfc_iso14443a_data_t *identity,
                           nfc_desfire_card_t *out_card,
                           nfc_desfire_cancel_cb_t cancel) {
  if (!identity || !out_card || out_card->apps)
    return ESP_ERR_INVALID_ARG;
  nfc_desfire_card_t card = {0};
  mf_desfire_session_t session = {0};
  esp_err_t error = mf_desfire_poller_init(identity, &session);
  if (error != ESP_OK)
    goto cleanup;
  session.should_cancel = cancel;
  if ((identity->uid_len != UID_SHORT_SIZE && identity->uid_len != UID_LONG_SIZE) ||
      session.dep.ats_len > T4T_EMU_ATS_CAPACITY) {
    error = ESP_ERR_INVALID_RESPONSE;
    goto cleanup;
  }
  memcpy(card.identity.uid, identity->uid, identity->uid_len);
  card.identity.uid_len = identity->uid_len;
  memcpy(card.identity.atqa, identity->atqa, sizeof(card.identity.atqa));
  card.identity.sak = identity->sak;
  memcpy(card.identity.ats, session.dep.ats, session.dep.ats_len);
  card.identity.ats_len = session.dep.ats_len;
  size_t length = 0;
  error = command(&session,
                  MF_DESFIRE_CMD_GET_VERSION,
                  NULL,
                  0,
                  card.version,
                  sizeof(card.version),
                  &length,
                  false);
  if (error != ESP_OK) {
    session.use_native_frames = true;
    error = command(&session,
                    MF_DESFIRE_CMD_GET_VERSION,
                    NULL,
                    0,
                    card.version,
                    sizeof(card.version),
                    &length,
                    false);
  }
  if (error != ESP_OK)
    goto cleanup;
  uint8_t type = card.version[VERSION_TYPE_OFFSET] & TYPE_MASK;
  if (length != sizeof(card.version) || (type != TYPE_DESFIRE && type != TYPE_LIGHT)) {
    error = ESP_ERR_INVALID_RESPONSE;
    goto cleanup;
  }
  uint8_t buffer[NFC_DESFIRE_MAX_APPS * NFC_DESFIRE_AID_SIZE];
  error = command(&session, CMD_FREE_MEMORY, NULL, 0, buffer, sizeof(buffer), &length, true);
  if (error == ESP_OK) {
    if (length != NFC_DESFIRE_AID_SIZE) {
      error = ESP_ERR_INVALID_RESPONSE;
      goto cleanup;
    }
    card.has_free_memory = true;
    card.free_memory = little(buffer, length);
  } else if (error != ESP_ERR_NOT_FOUND)
    goto cleanup;
  error = read_keys(&session, &card.keys, cancel);
  if (error != ESP_OK)
    goto cleanup;
  error =
      command(&session, MF_DESFIRE_CMD_GET_APP_IDS, NULL, 0, buffer, sizeof(buffer), &length, true);
  if (error == ESP_ERR_NOT_FOUND) {
    error = ESP_OK;
    goto success;
  }
  if (error != ESP_OK)
    goto cleanup;
  if (length % NFC_DESFIRE_AID_SIZE) {
    error = ESP_ERR_INVALID_RESPONSE;
    goto cleanup;
  }
  card.has_apps = true;
  size_t count = length / NFC_DESFIRE_AID_SIZE;
  if (count) {
    card.apps = calloc(count, sizeof(*card.apps));
    if (!card.apps) {
      ESP_LOGE(TAG, "Cannot allocate applications");
      error = ESP_ERR_NO_MEM;
      goto cleanup;
    }
  }
  card.app_count = count;
  for (size_t a = 0; a < count; a++) {
    memcpy(card.apps[a].aid, buffer + a * NFC_DESFIRE_AID_SIZE, NFC_DESFIRE_AID_SIZE);
    for (size_t b = 0; b < a; b++)
      if (!memcmp(card.apps[a].aid, card.apps[b].aid, NFC_DESFIRE_AID_SIZE)) {
        error = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
      }
  }
  size_t total = 0;
  for (size_t a = 0; a < count; a++) {
    if (cancel && cancel()) {
      error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    nfc_desfire_app_t *app = &card.apps[a];
    error = command(&session,
                    MF_DESFIRE_CMD_SELECT_APP,
                    app->aid,
                    sizeof(app->aid),
                    buffer,
                    sizeof(buffer),
                    &length,
                    false);
    if (error != ESP_OK)
      goto cleanup;
    error = read_keys(&session, &app->keys, cancel);
    if (error != ESP_OK)
      goto cleanup;
    error = command(&session,
                    MF_DESFIRE_CMD_GET_FILE_IDS,
                    NULL,
                    0,
                    buffer,
                    NFC_DESFIRE_MAX_FILES,
                    &length,
                    true);
    if (error == ESP_ERR_NOT_FOUND)
      continue;
    if (error != ESP_OK)
      goto cleanup;
    app->has_files = true;
    if (length) {
      app->files = calloc(length, sizeof(*app->files));
      if (!app->files) {
        ESP_LOGE(TAG, "Cannot allocate files");
        error = ESP_ERR_NO_MEM;
        goto cleanup;
      }
    }
    app->file_count = length;
    for (size_t f = 0; f < length; f++) {
      if (buffer[f] >= NFC_DESFIRE_MAX_FILES) {
        error = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
      }
      for (size_t g = 0; g < f; g++)
        if (buffer[g] == buffer[f]) {
          error = ESP_ERR_INVALID_RESPONSE;
          goto cleanup;
        }
      app->files[f].id = buffer[f];
    }
    for (size_t f = 0; f < app->file_count; f++) {
      if (cancel && cancel()) {
        error = ESP_ERR_INVALID_STATE;
        goto cleanup;
      }
      nfc_desfire_file_t *file = &app->files[f];
      error = command(
          &session, CMD_FILE_SETTINGS, &file->id, 1, buffer, SETTINGS_BUFFER, &length, true);
      if (error == ESP_ERR_NOT_FOUND)
        continue;
      if (error != ESP_OK)
        goto cleanup;
      error = nfc_desfire_parse_settings(buffer, length, file);
      if (error != ESP_OK)
        goto cleanup;
      error = read_contents(&session, file, &total, cancel);
      if (error != ESP_OK)
        goto cleanup;
    }
  }
success:
  *out_card = card;
  return ESP_OK;
cleanup:
  nfc_desfire_free(&card);
  return error;
}

static size_t key_pages(const nfc_desfire_keys_t *keys) {
  return (keys->count + VIEW_KEYS - 1) / VIEW_KEYS;
}

static size_t data_pages(const nfc_desfire_file_t *file) {
  return file->has_data ? (file->length + VIEW_BYTES - 1) / VIEW_BYTES : 0;
}

size_t nfc_desfire_page_count(const nfc_desfire_card_t *card) {
  if (!card)
    return 0;
  size_t count = 1 + key_pages(&card->keys);
  for (size_t a = 0; a < card->app_count; a++) {
    count += 1 + key_pages(&card->apps[a].keys);
    for (size_t f = 0; f < card->apps[a].file_count; f++)
      count += 2 + data_pages(&card->apps[a].files[f]);
  }
  return count;
}

static void key_page(const nfc_desfire_keys_t *keys, size_t page, char *out, size_t capacity) {
  size_t used = 0;
  for (size_t i = page * VIEW_KEYS; i < keys->count && i < (page + 1) * VIEW_KEYS; i++) {
    int length = snprintf(out + used,
                          capacity - used,
                          "Key %u version: %s%02X\n",
                          (unsigned)i,
                          keys->has_version[i] ? "" : "unknown ",
                          keys->versions[i]);
    if (length < 0 || (size_t)length >= capacity - used)
      return;
    used += length;
  }
}

void nfc_desfire_page(const nfc_desfire_card_t *card,
                      size_t page,
                      char *out_text,
                      size_t capacity) {
  if (!card || !out_text || !capacity)
    return;
  out_text[0] = 0;
  if (!page) {
    char uid[UID_LONG_SIZE * 2 + 1] = {0};
    for (size_t i = 0; i < card->identity.uid_len && i < UID_LONG_SIZE; i++)
      snprintf(uid + i * 2, sizeof(uid) - i * 2, "%02X", card->identity.uid[i]);
    snprintf(out_text,
             capacity,
             "UID %s\nDESFire HW %u SW %u\nApplications: %u (%s)\nFree memory: %s %lu\nMaster "
             "keys: %u (%s)\nSettings %02X flags %02X\nATQA %02X%02X SAK %02X ATS %u B",
             uid,
             card->version[VERSION_HW_MAJOR],
             card->version[VERSION_SW_MAJOR],
             (unsigned)card->app_count,
             card->has_apps ? "known" : "unavailable",
             card->has_free_memory ? "bytes" : "unknown",
             (unsigned long)card->free_memory,
             card->keys.count,
             card->keys.has_settings ? "known" : "unavailable",
             card->keys.settings,
             card->keys.flags,
             card->identity.atqa[1],
             card->identity.atqa[0],
             card->identity.sak,
             card->identity.ats_len);
    return;
  }
  page--;
  if (page < key_pages(&card->keys)) {
    key_page(&card->keys, page, out_text, capacity);
    return;
  }
  page -= key_pages(&card->keys);
  for (size_t a = 0; a < card->app_count; a++) {
    const nfc_desfire_app_t *app = &card->apps[a];
    if (!page) {
      snprintf(out_text,
               capacity,
               "AID %02X%02X%02X\nKeys: %u (%s)\nFiles: %u (%s)\nSettings %02X flags %02X",
               app->aid[0],
               app->aid[1],
               app->aid[2],
               app->keys.count,
               app->keys.has_settings ? "known" : "unavailable",
               (unsigned)app->file_count,
               app->has_files ? "known" : "unavailable",
               app->keys.settings,
               app->keys.flags);
      return;
    }
    page--;
    if (page < key_pages(&app->keys)) {
      key_page(&app->keys, page, out_text, capacity);
      return;
    }
    page -= key_pages(&app->keys);
    for (size_t f = 0; f < app->file_count; f++) {
      const nfc_desfire_file_t *file = &app->files[f];
      if (!page) {
        snprintf(out_text,
                 capacity,
                 "AID %02X%02X%02X File %u\nType %u Comm %u (%s)\nSize %lu Records %lu/%lu\nData: "
                 "%u bytes (%s)\nValue limits %ld..%ld\nCredit %ld enabled %u",
                 app->aid[0],
                 app->aid[1],
                 app->aid[2],
                 file->id,
                 file->type,
                 file->communication,
                 file->has_settings ? "known" : "unavailable",
                 (unsigned long)file->size,
                 (unsigned long)file->current,
                 (unsigned long)file->maximum,
                 (unsigned)file->length,
                 file->has_data ? "read" : "unavailable",
                 (long)(int32_t)file->low,
                 (long)(int32_t)file->high,
                 (long)(int32_t)file->credit,
                 file->limited_credit);
        return;
      }
      page--;
      if (!page) {
        int length = snprintf(
            out_text, capacity, "File %u access rights (%u)\n", file->id, file->rights_length / 2);
        if (length < 0 || (size_t)length >= capacity)
          return;
        size_t used = length;
        for (size_t i = 0; i < file->rights_length; i += 2) {
          length = snprintf(out_text + used,
                            capacity - used,
                            "%02X%02X%s",
                            file->rights[i + 1],
                            file->rights[i],
                            (i / 2 + 1) % VIEW_KEYS ? " " : "\n");
          if (length < 0 || (size_t)length >= capacity - used)
            return;
          used += length;
        }
        snprintf(out_text + used,
                 capacity - used,
                 "\nTMAC option %02X version %02X limit %lu",
                 file->key_option,
                 file->key_version,
                 (unsigned long)file->counter_limit);
        return;
      }
      page--;
      size_t pages = data_pages(file);
      if (page < pages) {
        size_t offset = page * VIEW_BYTES;
        int length = snprintf(out_text,
                              capacity,
                              "AID %02X%02X%02X File %u\nOffset %u / %u\n",
                              app->aid[0],
                              app->aid[1],
                              app->aid[2],
                              file->id,
                              (unsigned)offset,
                              (unsigned)file->length);
        if (length < 0 || (size_t)length >= capacity)
          return;
        size_t used = length;
        for (size_t i = offset; i < file->length && i < offset + VIEW_BYTES; i++) {
          length = snprintf(out_text + used,
                            capacity - used,
                            "%02X%s",
                            file->data[i],
                            (i + 1) % VIEW_ROW_BYTES ? " " : "\n");
          if (length < 0 || (size_t)length >= capacity - used)
            return;
          used += length;
        }
        return;
      }
      page -= pages;
    }
  }
}
