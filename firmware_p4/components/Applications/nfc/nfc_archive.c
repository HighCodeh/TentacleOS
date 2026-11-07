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

#include "nfc_archive.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"

#include "storage_atomic.h"

#define MAX_FIELDS     4096U
#define COPY_LIMIT     10000U
#define UID_MAX_BYTES  10U
#define HEX_BYTE_CHARS 3U
#define KEY_MAX_BYTES  128U

static const char *TAG = "NFC_ARCHIVE";

static esp_err_t validate(nfc_archive_t *archive);
static esp_err_t append_hex(
    char *out, size_t capacity, size_t *used, const char *name, const uint8_t *data, size_t length);

esp_err_t nfc_archive_load(const char *path, nfc_archive_t *out_archive) {
  if (!path || !out_archive)
    return ESP_ERR_INVALID_ARG;
  memset(out_archive, 0, sizeof(*out_archive));
  FILE *file = fopen(path, "rb");
  if (!file)
    return ESP_ERR_NOT_FOUND;
  esp_err_t error = ESP_ERR_INVALID_SIZE;
  if (fseek(file, 0, SEEK_END) != 0)
    goto cleanup;
  long size = ftell(file);
  if (size <= 0 || size > NFC_ARCHIVE_MAX_BYTES || fseek(file, 0, SEEK_SET) != 0)
    goto cleanup;
  out_archive->data = malloc((size_t)size + 1);
  if (!out_archive->data) {
    ESP_LOGE(TAG, "Cannot allocate archive");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  out_archive->length = (size_t)size;
  if (fread(out_archive->data, 1, (size_t)size, file) != (size_t)size || ferror(file)) {
    error = ESP_FAIL;
    goto cleanup;
  }
  out_archive->data[size] = 0;
  error = validate(out_archive);
cleanup:
  fclose(file);
  if (error != ESP_OK)
    nfc_archive_free(out_archive);
  return error;
}

void nfc_archive_free(nfc_archive_t *archive) {
  if (!archive)
    return;
  if (archive->data)
    memset(archive->data, 0, archive->length);
  free(archive->data);
  memset(archive, 0, sizeof(*archive));
}

esp_err_t nfc_archive_export(const nfc_archive_t *archive, char *out_path, size_t capacity) {
  if (!archive || !out_path || capacity < NFC_ARCHIVE_PATH_SIZE)
    return ESP_ERR_INVALID_ARG;
  out_path[0] = 0;
  nfc_archive_t checked = *archive;
  esp_err_t error = validate(&checked);
  if (error != ESP_OK)
    return error;
  if ((mkdir("/sdcard/nfc", S_IRWXU) != 0 && errno != EEXIST) ||
      (mkdir(NFC_ARCHIVE_DIRECTORY, S_IRWXU) != 0 && errno != EEXIST))
    return ESP_FAIL;
  char path[NFC_ARCHIVE_PATH_SIZE];
  int fd = -1;
  for (unsigned index = 0; index < COPY_LIMIT; index++) {
    snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/archive_%04u.nfc", index);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd >= 0 || errno != EEXIST)
      break;
  }
  if (fd < 0)
    return ESP_FAIL;
  error = close(fd) == 0 ? storage_write_atomic(path, archive->data, archive->length) : ESP_FAIL;
  if (error != ESP_OK) {
    unlink(path);
    return error;
  }
  memcpy(out_path, path, strlen(path) + 1);
  return ESP_OK;
}

esp_err_t nfc_archive_capture(const char *device,
                              const uint8_t *uid,
                              size_t uid_length,
                              const nfc_archive_field_t *fields,
                              size_t count,
                              char *out_path,
                              size_t capacity) {
  if (!device || strchr(device, '\n') || strchr(device, '\r') || !uid || !uid_length ||
      uid_length > UID_MAX_BYTES || !fields || !count || count > MAX_FIELDS)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = ESP_ERR_NO_MEM;
  nfc_archive_t archive = {0};
  archive.data = malloc(NFC_ARCHIVE_MAX_BYTES + 1);
  if (!archive.data) {
    ESP_LOGE(TAG, "Cannot allocate capture");
    goto cleanup;
  }
  int header = snprintf(
      archive.data,
      NFC_ARCHIVE_MAX_BYTES,
      "# Copyright (c) 2025 HIGH CODE LLC\n#\n# TentacleOS is free software: you can redistribute "
      "it and/or modify\n# it under the terms of the GNU General Public License as published by\n# "
      "the Free Software Foundation, either version 3 of the License, or\n# (at your option) any "
      "later version.\n#\n# TentacleOS is distributed in the hope that it will be useful,\n# but "
      "WITHOUT ANY WARRANTY; without even the implied warranty of\n# MERCHANTABILITY or FITNESS "
      "FOR A PARTICULAR PURPOSE. See the\n# GNU General Public License for more details.\n#\n# You "
      "should have received a copy of the GNU General Public License\n# along with TentacleOS. If "
      "not, see <https://www.gnu.org/licenses/>.\n"
      "Filetype: Highboy NFC capture\nVersion: 1\nDevice type: %s\n"
      "Capture scope: Only fields actually read; not a full-card claim\n",
      device);
  if (header < 0 || (size_t)header >= NFC_ARCHIVE_MAX_BYTES) {
    error = ESP_ERR_INVALID_SIZE;
    goto cleanup;
  }
  archive.length = (size_t)header;
  error = append_hex(archive.data, NFC_ARCHIVE_MAX_BYTES, &archive.length, "UID", uid, uid_length);
  for (size_t i = 0; error == ESP_OK && i < count; i++)
    error = append_hex(archive.data,
                       NFC_ARCHIVE_MAX_BYTES,
                       &archive.length,
                       fields[i].name,
                       fields[i].data,
                       fields[i].length);
  if (error == ESP_OK)
    error = nfc_archive_export(&archive, out_path, capacity);
cleanup:
  nfc_archive_free(&archive);
  return error;
}

static int compare_keys(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static char *trim(char *text) {
  while (*text == ' ' || *text == '\t')
    text++;
  size_t length = strlen(text);
  while (length &&
         (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r'))
    text[--length] = 0;
  return text;
}

static esp_err_t validate(nfc_archive_t *archive) {
  if (!archive->data || !archive->length || archive->length > NFC_ARCHIVE_MAX_BYTES)
    return ESP_ERR_INVALID_SIZE;
  for (size_t i = 0; i < archive->length; i++) {
    unsigned char c = (unsigned char)archive->data[i];
    if ((!c || c < ' ') && c != '\r' && c != '\n' && c != '\t')
      return ESP_ERR_INVALID_RESPONSE;
  }
  char *copy = malloc(archive->length + 1);
  char **keys = malloc(MAX_FIELDS * sizeof(*keys));
  esp_err_t error = ESP_ERR_NO_MEM;
  if (!copy || !keys) {
    ESP_LOGE(TAG, "Cannot allocate validation buffers");
    goto cleanup;
  }
  memcpy(copy, archive->data, archive->length);
  copy[archive->length] = 0;
  error = ESP_ERR_INVALID_RESPONSE;
  char *filetype = NULL, *version = NULL, *device = NULL, *uid = NULL;
  size_t count = 0, ndef_bytes = 0;
  char *ndef_size = NULL;
  bool has_ndef_chunks = false;
  char *state = NULL;
  for (char *line = strtok_r(copy, "\n", &state); line; line = strtok_r(NULL, "\n", &state)) {
    line = trim(line);
    if (!*line || *line == '#')
      continue;
    char *colon = strchr(line, ':');
    if (!colon || count == MAX_FIELDS)
      goto cleanup;
    *colon = 0;
    char *key = trim(line), *value = trim(colon + 1);
    if (!*key || strlen(key) > KEY_MAX_BYTES)
      goto cleanup;
    keys[count++] = key;
    if (!strcmp(key, "NDEF Data Size"))
      ndef_size = value;
    if (!strcmp(key, "NDEF Data")) {
      has_ndef_chunks = true;
      size_t chunk = 0;
      const char *cursor = value;
      while (*cursor) {
        if (!isxdigit((unsigned char)cursor[0]) || !cursor[1] ||
            !isxdigit((unsigned char)cursor[1]))
          goto cleanup;
        cursor += 2;
        chunk++;
        if (*cursor && *cursor != ' ' && *cursor != '\t')
          goto cleanup;
        while (*cursor == ' ' || *cursor == '\t')
          cursor++;
      }
      if (!chunk || chunk > 16)
        goto cleanup;
      ndef_bytes += chunk;
    }
    if (!strcmp(key, "Filetype"))
      filetype = value;
    else if (!strcmp(key, "Version"))
      version = value;
    else if (!strcmp(key, "Device type"))
      device = value;
    else if (!strcmp(key, "UID"))
      uid = value;
  }
  if (!filetype || !version || !device || !*device || !uid || !*uid || count <= 4)
    goto cleanup;
  bool is_capture = !strcmp(filetype, "Highboy NFC capture");
  if (is_capture ? strcmp(version, "1")
                 : (strcmp(filetype, "Flipper NFC device") || strcmp(version, "4")))
    goto cleanup;
  size_t uid_bytes = 0;
  const char *cursor = uid;
  while (*cursor) {
    if (!isxdigit((unsigned char)cursor[0]) || !cursor[1] || !isxdigit((unsigned char)cursor[1]))
      goto cleanup;
    cursor += 2;
    uid_bytes++;
    if (*cursor && *cursor != ' ' && *cursor != '\t')
      goto cleanup;
    while (*cursor == ' ' || *cursor == '\t')
      cursor++;
  }
  if (!uid_bytes || uid_bytes > UID_MAX_BYTES || strlen(uid) >= sizeof(archive->uid) ||
      strlen(device) >= sizeof(archive->device))
    goto cleanup;
  bool is_type4 = !is_capture && !strcmp(device, "Type 4 Tag");
  if (is_type4) {
    if (!ndef_size || !*ndef_size || strspn(ndef_size, "0123456789") != strlen(ndef_size) ||
        strtoul(ndef_size, NULL, 10) != ndef_bytes)
      goto cleanup;
  } else if (has_ndef_chunks)
    goto cleanup;
  qsort(keys, count, sizeof(*keys), compare_keys);
  for (size_t i = 1; i < count; i++) {
    if (!strcmp(keys[i - 1], keys[i]) && !(is_type4 && !strcmp(keys[i], "NDEF Data")))
      goto cleanup;
  }
  memcpy(archive->device, device, strlen(device) + 1);
  memcpy(archive->uid, uid, strlen(uid) + 1);
  archive->is_capture = is_capture;
  archive->fields = count;
  error = ESP_OK;
cleanup:
  if (copy)
    memset(copy, 0, archive->length);
  free(copy);
  free(keys);
  return error;
}

static esp_err_t append_hex(char *out,
                            size_t capacity,
                            size_t *used,
                            const char *name,
                            const uint8_t *data,
                            size_t length) {
  if (!name || !*name || strlen(name) > KEY_MAX_BYTES || strpbrk(name, ":\r\n") ||
      (length && !data))
    return ESP_ERR_INVALID_ARG;
  size_t key_length = strlen(name);
  if (*used >= capacity || key_length + 3 > capacity - *used ||
      length > (capacity - *used - key_length - 3) / HEX_BYTE_CHARS)
    return ESP_ERR_INVALID_SIZE;
  memcpy(out + *used, name, key_length);
  *used += key_length;
  out[(*used)++] = ':';
  for (size_t i = 0; i < length; i++) {
    snprintf(out + *used, capacity - *used, " %02X", data[i]);
    *used += HEX_BYTE_CHARS;
  }
  out[(*used)++] = '\n';
  out[*used] = 0;
  return ESP_OK;
}
