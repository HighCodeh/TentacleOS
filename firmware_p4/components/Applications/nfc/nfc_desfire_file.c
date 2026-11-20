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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "storage_atomic.h"

#define TEXT_LIMIT          (1024U * 1024U)
#define FIELD_LIMIT         65536U
#define KEY_LIMIT           128U
#define HEX_BASE            16U
#define DEC_BASE            10U
#define NIBBLE_BITS         4U
#define NIBBLE_MASK         0x0FU
#define ISO_DEP_SAK         0x20U
#define ATS_FIRST_INTERFACE 0x10U
#define ATS_INTERFACE_COUNT 3U
#define FILE_VALUE          2U
#define FILE_LINEAR         3U
#define FILE_CYCLIC         4U
#define FILE_TMAC           5U
#define SIZE24_MAX          0xFFFFFFU
#define VALUE_BYTES         4U
#define UID_SHORT           4U
#define UID_LONG            7U
#define KEY_FLAGS_MASK      0x0FU
#define VERSION_TYPE_INDEX  1U
#define TYPE_DESFIRE        0x01U
#define TYPE_LIGHT          0x08U

typedef struct {
  char *key, *value;
} nfc_desfire_field_t;

typedef struct {
  bool is_loading;
  char *text;
  size_t used, count, total_data;
  nfc_desfire_field_t *fields;
  esp_err_t error;
} nfc_desfire_codec_t;

static const char *TAG = "NFC_DESFIRE_FILE";

static char *trim(char *text) {
  while (isspace((unsigned char)*text))
    text++;
  size_t size = strlen(text);
  while (size && isspace((unsigned char)text[size - 1]))
    text[--size] = 0;
  return text;
}

static int compare(const void *left, const void *right) {
  return strcmp(((const nfc_desfire_field_t *)left)->key,
                ((const nfc_desfire_field_t *)right)->key);
}

static const char *lookup(nfc_desfire_codec_t *codec, const char *key) {
  nfc_desfire_field_t query = {.key = (char *)key};
  nfc_desfire_field_t *field =
      bsearch(&query, codec->fields, codec->count, sizeof(*codec->fields), compare);
  return field ? field->value : NULL;
}

static bool append(nfc_desfire_codec_t *codec, const char *text) {
  size_t length = strlen(text);
  if (length >= TEXT_LIMIT - codec->used)
    return false;
  memcpy(codec->text + codec->used, text, length + 1);
  codec->used += length;
  return true;
}

static bool field_name(char *out, const char *prefix, const char *name) {
  int size = snprintf(out, KEY_LIMIT, "%s%s%s", prefix, *prefix && *name ? " " : "", name);
  return size >= 0 && (size_t)size < KEY_LIMIT;
}

static bool text_field(nfc_desfire_codec_t *codec, const char *key, const char *value) {
  if (codec->is_loading) {
    const char *found = lookup(codec, key);
    return found && !strcmp(found, value);
  }
  return append(codec, key) && append(codec, ": ") && append(codec, value) && append(codec, "\n");
}

static bool hex(nfc_desfire_codec_t *codec,
                const char *key,
                uint8_t *bytes,
                size_t minimum,
                size_t maximum,
                size_t *out_size) {
  if (!codec->is_loading) {
    size_t size = *out_size;
    if (size < minimum || size > maximum || (size && !bytes))
      return false;
    if (!append(codec, key) || !append(codec, ": "))
      return false;
    for (size_t i = 0; i < size; i++) {
      char byte[4];
      snprintf(byte, sizeof(byte), "%02X%s", bytes[i], i + 1 == size ? "" : " ");
      if (!append(codec, byte))
        return false;
    }
    return append(codec, "\n");
  }
  const char *value = lookup(codec, key);
  if (!value)
    return false;
  size_t count = 0;
  while (*value) {
    while (isspace((unsigned char)*value))
      value++;
    if (!*value)
      break;
    if (!isxdigit((unsigned char)value[0]) || !isxdigit((unsigned char)value[1]) ||
        (value[2] && !isspace((unsigned char)value[2])) || count == maximum)
      return false;
    char pair[3] = {value[0], value[1], 0};
    if (bytes)
      bytes[count] = strtoul(pair, NULL, HEX_BASE);
    count++;
    value += 2;
  }
  *out_size = count;
  return count >= minimum;
}

static bool bytes_field(
    nfc_desfire_codec_t *codec, const char *prefix, const char *name, uint8_t *bytes, size_t size) {
  char key[KEY_LIMIT];
  return field_name(key, prefix, name) && hex(codec, key, bytes, size, size, &size);
}

static bool
number(nfc_desfire_codec_t *codec, const char *prefix, const char *name, uint32_t *value) {
  char key[KEY_LIMIT];
  if (!field_name(key, prefix, name))
    return false;
  if (codec->is_loading) {
    const char *text = lookup(codec, key);
    if (!text || !isdigit((unsigned char)*text))
      return false;
    errno = 0;
    char *end;
    unsigned long parsed = strtoul(text, &end, DEC_BASE);
    if (errno || *end || parsed > UINT32_MAX)
      return false;
    *value = parsed;
    return true;
  }
  char text[32];
  snprintf(text, sizeof(text), "%lu", (unsigned long)*value);
  return text_field(codec, key, text);
}

static bool boolean(nfc_desfire_codec_t *codec, const char *prefix, const char *name, bool *value) {
  char key[KEY_LIMIT];
  if (!field_name(key, prefix, name))
    return false;
  if (codec->is_loading) {
    const char *text = lookup(codec, key);
    if (!text || (strcmp(text, "true") && strcmp(text, "false")))
      return false;
    *value = !strcmp(text, "true");
    return true;
  }
  return text_field(codec, key, *value ? "true" : "false");
}

/* Optional extensions distinguish unavailable fields from genuine zero values. */
static bool
presence(nfc_desfire_codec_t *codec, const char *prefix, const char *name, bool *value) {
  char key[KEY_LIMIT];
  if (!field_name(key, prefix, name))
    return false;
  if (codec->is_loading && !lookup(codec, key)) {
    *value = true;
    return true;
  }
  return boolean(codec, prefix, name, value);
}

static bool keys_fields(nfc_desfire_codec_t *codec, const char *prefix, nfc_desfire_keys_t *keys) {
  if (!presence(codec, prefix, "Highboy Settings Known", &keys->has_settings))
    return false;
  uint8_t change = keys->settings >> NIBBLE_BITS;
  if (!bytes_field(codec, prefix, "Change Key ID", &change, 1) || change > NIBBLE_MASK)
    return false;
  if (codec->is_loading)
    keys->settings = change << NIBBLE_BITS;
  const char *names[] = {
      "Key Changeable", "Free Directory List", "Free Create Delete", "Config Changeable"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    bool value = (keys->settings & (1U << i)) != 0;
    if (!boolean(codec, prefix, names[i], &value))
      return false;
    if (codec->is_loading && value)
      keys->settings |= 1U << i;
  }
  char flags_key[KEY_LIMIT];
  if (!field_name(flags_key, prefix, "Flags"))
    return false;
  if (!codec->is_loading || lookup(codec, flags_key)) {
    if (!bytes_field(codec, prefix, "Flags", &keys->flags, 1) || (keys->flags & ~KEY_FLAGS_MASK))
      return false;
  }
  if (!bytes_field(codec, prefix, "Max Keys", &keys->count, 1) ||
      keys->count > NFC_DESFIRE_MAX_KEYS)
    return false;
  for (size_t i = 0; i < keys->count; i++) {
    char name[KEY_LIMIT];
    snprintf(name, sizeof(name), "Key %u Known", (unsigned)i);
    if (!presence(codec, prefix, name, &keys->has_version[i]))
      return false;
    snprintf(name, sizeof(name), "Key %u Version", (unsigned)i);
    if (!bytes_field(codec, prefix, name, &keys->versions[i], 1))
      return false;
  }
  return true;
}

static bool file_fields(nfc_desfire_codec_t *codec, const char *prefix, nfc_desfire_file_t *file) {
  if (!presence(codec, prefix, "Highboy Settings Known", &file->has_settings))
    return false;
  if (!file->has_settings)
    return true;
  if (!bytes_field(codec, prefix, "Type", &file->type, 1) || file->type > FILE_TMAC ||
      !bytes_field(codec, prefix, "Communication Settings", &file->communication, 1) ||
      (file->communication != 0 && file->communication != 1 && file->communication != 3))
    return false;
  char key[KEY_LIMIT];
  size_t rights = file->rights_length;
  if (!field_name(key, prefix, "Access Rights") ||
      !hex(codec, key, file->rights, 2, sizeof(file->rights), &rights) || rights % 2)
    return false;
  file->rights_length = rights;
  size_t expected = 0;
  if (file->type < FILE_VALUE) {
    if (!number(codec, prefix, "Size", &file->size) || file->size > SIZE24_MAX)
      return false;
    expected = file->size;
  } else if (file->type == FILE_VALUE) {
    if (!number(codec, prefix, "Hi Limit", &file->high) ||
        !number(codec, prefix, "Lo Limit", &file->low) ||
        !number(codec, prefix, "Limited Credit Value", &file->credit) ||
        !boolean(codec, prefix, "Limited Credit Enabled", &file->limited_credit))
      return false;
    file->size = VALUE_BYTES;
    expected = VALUE_BYTES;
  } else if (file->type < FILE_TMAC) {
    if (!number(codec, prefix, "Size", &file->size) ||
        !number(codec, prefix, "Max", &file->maximum) ||
        !number(codec, prefix, "Cur", &file->current) || !file->size || file->size > SIZE24_MAX ||
        file->maximum > SIZE24_MAX || file->current > file->maximum)
      return false;
    if (file->current && file->size > SIZE_MAX / file->current)
      return false;
    expected = (size_t)file->size * file->current;
  } else {
    if (!bytes_field(codec, prefix, "Key Option", &file->key_option, 1) ||
        !bytes_field(codec, prefix, "Key Version", &file->key_version, 1) ||
        !number(codec, prefix, "Counter Limit", &file->counter_limit))
      return false;
  }
  if (codec->is_loading) {
    file->has_data = lookup(codec, prefix) != NULL;
    if (!file->has_data)
      return true;
    size_t size = 0;
    if (!hex(codec, prefix, NULL, 0, NFC_DESFIRE_MAX_DATA - codec->total_data, &size) ||
        size != expected)
      return false;
    if (size) {
      file->data = malloc(size);
      if (!file->data) {
        ESP_LOGE(TAG, "Cannot allocate loaded file");
        codec->error = ESP_ERR_NO_MEM;
        return false;
      }
    }
    file->length = size;
  }
  if (!file->has_data)
    return file->length == 0;
  if (file->length != expected || file->length > NFC_DESFIRE_MAX_DATA - codec->total_data)
    return false;
  codec->total_data += file->length;
  return hex(codec, prefix, file->data, file->length, file->length, &file->length);
}

static bool identity_fields(nfc_desfire_codec_t *codec, t4t_emu_identity_t *identity) {
  size_t uid_length = identity->uid_len;
  if (!hex(codec, "UID", identity->uid, UID_SHORT, UID_LONG, &uid_length) ||
      (uid_length != UID_SHORT && uid_length != UID_LONG))
    return false;
  identity->uid_len = uid_length;
  uint8_t atqa[] = {identity->atqa[1], identity->atqa[0]};
  if (!bytes_field(codec, "", "ATQA", atqa, sizeof(atqa)) ||
      !bytes_field(codec, "", "SAK", &identity->sak, 1) || !(identity->sak & ISO_DEP_SAK))
    return false;
  identity->atqa[0] = atqa[1];
  identity->atqa[1] = atqa[0];
  if (!codec->is_loading && (identity->ats_len < 1 || identity->ats_len > T4T_EMU_ATS_CAPACITY ||
                             identity->ats[0] != identity->ats_len))
    return false;
  size_t pos = 1;
  if (codec->is_loading ? lookup(codec, "T0") != NULL : identity->ats_len > 1) {
    if (!bytes_field(codec, "", "T0", &identity->ats[pos++], 1))
      return false;
    const char *names[] = {"TA(1)", "TB(1)", "TC(1)"};
    for (size_t i = 0; i < ATS_INTERFACE_COUNT; i++) {
      if (!(identity->ats[1] & (ATS_FIRST_INTERFACE << i)))
        continue;
      if (!codec->is_loading && pos >= identity->ats_len)
        return false;
      if (!bytes_field(codec, "", names[i], &identity->ats[pos++], 1))
        return false;
    }
    size_t historical = codec->is_loading ? 0 : identity->ats_len - pos;
    if (codec->is_loading ? lookup(codec, "T1...Tk") != NULL : historical != 0) {
      if (!hex(codec, "T1...Tk", identity->ats + pos, 0, T4T_EMU_ATS_CAPACITY - pos, &historical))
        return false;
      pos += historical;
    }
  }
  identity->ats_len = pos;
  identity->ats[0] = pos;
  return true;
}

static bool card_fields(nfc_desfire_codec_t *codec, nfc_desfire_card_t *card) {
  if (!text_field(codec, "Filetype", "Flipper NFC device") || !text_field(codec, "Version", "4") ||
      !text_field(codec, "Device type", "Mifare DESFire") ||
      !identity_fields(codec, &card->identity) ||
      !bytes_field(codec, "PICC", "Version", card->version, sizeof(card->version)))
    return false;
  uint8_t family = card->version[VERSION_TYPE_INDEX] & NIBBLE_MASK;
  if (family != TYPE_DESFIRE && family != TYPE_LIGHT)
    return false;
  if (codec->is_loading)
    card->has_free_memory = lookup(codec, "PICC Free Memory") != NULL;
  if (card->has_free_memory && !number(codec, "PICC", "Free Memory", &card->free_memory))
    return false;
  if (!keys_fields(codec, "PICC", &card->keys) ||
      !presence(codec, "Application", "Highboy List Known", &card->has_apps))
    return false;
  uint32_t count = card->app_count;
  if (!number(codec, "Application", "Count", &count) || count > NFC_DESFIRE_MAX_APPS)
    return false;
  if (codec->is_loading && count) {
    card->apps = calloc(count, sizeof(*card->apps));
    if (!card->apps) {
      ESP_LOGE(TAG, "Cannot allocate loaded applications");
      codec->error = ESP_ERR_NO_MEM;
      return false;
    }
  }
  card->app_count = count;
  if (!count)
    return true;
  if (!card->apps || !card->has_apps)
    return false;
  uint8_t ids[NFC_DESFIRE_MAX_APPS * NFC_DESFIRE_AID_SIZE];
  if (!codec->is_loading)
    for (size_t a = 0; a < count; a++)
      memcpy(ids + a * NFC_DESFIRE_AID_SIZE, card->apps[a].aid, NFC_DESFIRE_AID_SIZE);
  if (!bytes_field(codec, "Application", "IDs", ids, count * NFC_DESFIRE_AID_SIZE))
    return false;
  for (size_t a = 0; a < count; a++) {
    nfc_desfire_app_t *app = &card->apps[a];
    memcpy(app->aid, ids + a * NFC_DESFIRE_AID_SIZE, NFC_DESFIRE_AID_SIZE);
    for (size_t b = 0; b < a; b++)
      if (!memcmp(app->aid, card->apps[b].aid, NFC_DESFIRE_AID_SIZE))
        return false;
    char prefix[KEY_LIMIT], key[KEY_LIMIT];
    snprintf(
        prefix, sizeof(prefix), "Application %02x%02x%02x", app->aid[0], app->aid[1], app->aid[2]);
    if (!keys_fields(codec, prefix, &app->keys))
      return false;
    if (!field_name(key, prefix, "File IDs"))
      return false;
    if (codec->is_loading)
      app->has_files = lookup(codec, key) != NULL;
    if (!app->has_files)
      continue;
    uint8_t files[NFC_DESFIRE_MAX_FILES];
    size_t file_count = app->file_count;
    if (file_count > NFC_DESFIRE_MAX_FILES || (file_count && !codec->is_loading && !app->files))
      return false;
    if (!codec->is_loading)
      for (size_t f = 0; f < file_count; f++)
        files[f] = app->files[f].id;
    if (!hex(codec, key, files, 0, sizeof(files), &file_count))
      return false;
    if (codec->is_loading && file_count) {
      app->files = calloc(file_count, sizeof(*app->files));
      if (!app->files) {
        ESP_LOGE(TAG, "Cannot allocate loaded file metadata");
        codec->error = ESP_ERR_NO_MEM;
        return false;
      }
    }
    app->file_count = file_count;
    for (size_t f = 0; f < file_count; f++) {
      if (files[f] >= NFC_DESFIRE_MAX_FILES)
        return false;
      for (size_t g = 0; g < f; g++)
        if (files[g] == files[f])
          return false;
      app->files[f].id = files[f];
      char sub[KEY_LIMIT];
      int size = snprintf(sub, sizeof(sub), "%s File %u", prefix, files[f]);
      if (size < 0 || (size_t)size >= sizeof(sub) || !file_fields(codec, sub, &app->files[f]))
        return false;
    }
  }
  return true;
}

esp_err_t nfc_desfire_load(const char *path, nfc_desfire_card_t *out_card) {
  if (!path || !out_card || out_card->apps)
    return ESP_ERR_INVALID_ARG;
  nfc_desfire_card_t card = {0};
  nfc_desfire_codec_t codec = {.is_loading = true, .error = ESP_ERR_INVALID_RESPONSE};
  FILE *file = fopen(path, "rb");
  if (!file)
    return ESP_ERR_NOT_FOUND;
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  if (fseek(file, 0, SEEK_END))
    goto cleanup;
  long length = ftell(file);
  if (length <= 0 || length >= TEXT_LIMIT || fseek(file, 0, SEEK_SET))
    goto cleanup;
  codec.text = malloc((size_t)length + 1);
  if (!codec.text) {
    ESP_LOGE(TAG, "Cannot allocate document");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  if (fread(codec.text, 1, length, file) != (size_t)length)
    goto cleanup;
  codec.text[length] = 0;
  for (long i = 0; i < length; i++) {
    unsigned char c = codec.text[i];
    if (!c || (c < ' ' && c != '\n' && c != '\r' && c != '\t'))
      goto cleanup;
    if (c == '\n')
      codec.count++;
  }
  if (++codec.count > FIELD_LIMIT)
    goto cleanup;
  codec.fields = calloc(codec.count, sizeof(*codec.fields));
  if (!codec.fields) {
    ESP_LOGE(TAG, "Cannot allocate document fields");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  codec.count = 0;
  char *line = codec.text;
  while (line && *line) {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = 0;
    char *key = trim(line);
    if (*key && *key != '#') {
      char *value = strchr(key, ':');
      if (!value)
        goto cleanup;
      *value++ = 0;
      key = trim(key);
      value = trim(value);
      if (!*key || strlen(key) >= KEY_LIMIT)
        goto cleanup;
      codec.fields[codec.count++] = (nfc_desfire_field_t){key, value};
    }
    line = next;
  }
  qsort(codec.fields, codec.count, sizeof(*codec.fields), compare);
  for (size_t i = 1; i < codec.count; i++)
    if (!strcmp(codec.fields[i - 1].key, codec.fields[i].key))
      goto cleanup;
  if (!card_fields(&codec, &card)) {
    error = codec.error;
    goto cleanup;
  }
  *out_card = card;
  memset(&card, 0, sizeof(card));
  error = ESP_OK;
cleanup:
  fclose(file);
  free(codec.fields);
  free(codec.text);
  nfc_desfire_free(&card);
  return error;
}

esp_err_t nfc_desfire_save(const char *path, const nfc_desfire_card_t *card) {
  if (!path || !card)
    return ESP_ERR_INVALID_ARG;
  nfc_desfire_codec_t codec = {.error = ESP_ERR_INVALID_RESPONSE};
  esp_err_t error = ESP_ERR_NO_MEM;
  codec.text = calloc(1, TEXT_LIMIT);
  if (!codec.text) {
    ESP_LOGE(TAG, "Cannot allocate serialized document");
    goto cleanup;
  }
  /* The codec writes normalized scalar values back; serialize a private typed copy. */
  nfc_desfire_card_t copy = *card;
  copy.apps = NULL;
  if (card->app_count > NFC_DESFIRE_MAX_APPS || (card->app_count && !card->apps)) {
    error = ESP_ERR_INVALID_ARG;
    goto cleanup;
  }
  if (card->app_count) {
    copy.apps = calloc(card->app_count, sizeof(*copy.apps));
    if (!copy.apps) {
      ESP_LOGE(TAG, "Cannot copy applications");
      goto cleanup;
    }
    for (size_t a = 0; a < card->app_count; a++) {
      copy.apps[a] = card->apps[a];
      copy.apps[a].files = NULL;
      size_t count = card->apps[a].file_count;
      if (count > NFC_DESFIRE_MAX_FILES || (count && !card->apps[a].files)) {
        error = ESP_ERR_INVALID_ARG;
        goto release;
      }
      if (count) {
        copy.apps[a].files = malloc(count * sizeof(*copy.apps[a].files));
        if (!copy.apps[a].files) {
          ESP_LOGE(TAG, "Cannot copy file metadata");
          goto release;
        }
        memcpy(copy.apps[a].files, card->apps[a].files, count * sizeof(*copy.apps[a].files));
      }
    }
  }
  if (!append(&codec,
              "# Copyright (c) 2025 HIGH CODE LLC\n#\n# TentacleOS is free software: you can "
              "redistribute it and/or modify\n# it under the terms of the GNU General Public "
              "License as published by\n# the Free Software Foundation, either version 3 of the "
              "License, or\n# (at your option) any later version.\n#\n# TentacleOS is distributed "
              "in the hope that it will be useful,\n# but WITHOUT ANY WARRANTY; without even the "
              "implied warranty of\n# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See "
              "the\n# GNU General Public License for more details.\n#\n# You should have received "
              "a copy of the GNU General Public License\n# along with TentacleOS. If not, see "
              "<https://www.gnu.org/licenses/>.\n") ||
      !card_fields(&codec, &copy)) {
    error = ESP_ERR_INVALID_RESPONSE;
    goto release;
  }
  error = storage_write_atomic(path, codec.text, codec.used);
release:
  for (size_t a = 0; a < card->app_count; a++)
    free(copy.apps[a].files);
  free(copy.apps);
cleanup:
  free(codec.text);
  return error;
}
