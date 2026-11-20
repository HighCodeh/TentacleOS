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

#include "nfc_dict_loader.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>

#include "esp_log.h"

static const char *TAG = "NFC_DICT";

#define MAX_LINE_LEN      96
#define MAX_KEY_SIZE      32
#define MAX_PATH_LEN      256
#define DIC_EXTENSION     ".dic"
#define DIC_EXTENSION_LEN 4

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

static bool parse_hex_line(const char *line, size_t key_size, uint8_t *out_key) {
  size_t got = 0;
  int hi = -1;

  for (const char *p = line; *p != '\0'; p++) {
    if (*p == '#' || *p == '\n' || *p == '\r') {
      break;
    }
    if (isspace((unsigned char)*p)) {
      continue;
    }

    int n = hex_nibble(*p);
    if (n < 0) {
      return false;
    }

    if (hi < 0) {
      hi = n;
    } else {
      if (got >= key_size) {
        return false;
      }
      out_key[got++] = (uint8_t)((hi << 4) | n);
      hi = -1;
    }
  }

  return (hi < 0) && (got == key_size);
}

typedef struct {
  size_t key_size;
  size_t max_keys;
} nfc_dict_io_t;

static esp_err_t
load_from(const char *path, const nfc_dict_io_t *io, uint8_t *out_keys, size_t *out_count) {
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    return ESP_ERR_NOT_FOUND;
  }

  if (io->key_size > MAX_KEY_SIZE) {
    ESP_LOGE(TAG, "key_size %u exceeds MAX_KEY_SIZE", (unsigned)io->key_size);
    fclose(f);
    return ESP_ERR_INVALID_ARG;
  }

  char line[MAX_LINE_LEN];
  size_t loaded = 0;
  size_t lineno = 0;

  while (loaded < io->max_keys && fgets(line, sizeof(line), f) != NULL) {
    lineno++;

    const char *p = line;
    while (*p != '\0' && isspace((unsigned char)*p)) {
      p++;
    }
    if (*p == '\0' || *p == '#') {
      continue;
    }

    uint8_t key[MAX_KEY_SIZE];
    if (!parse_hex_line(line, io->key_size, key)) {
      ESP_LOGD(TAG, "%s:%u: skip malformed line", path, (unsigned)lineno);
      continue;
    }

    memcpy(out_keys + loaded * io->key_size, key, io->key_size);
    loaded++;
  }

  fclose(f);
  *out_count = loaded;
  return (loaded > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t
nfc_dict_load_file(const nfc_dict_load_params_t *params, uint8_t *out_keys, size_t *out_count) {
  if (params == NULL || out_keys == NULL || out_count == NULL || params->key_size == 0 ||
      params->max_keys == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  *out_count = 0;

  const nfc_dict_io_t io = {.key_size = params->key_size, .max_keys = params->max_keys};

  if (params->sd_path != NULL) {
    esp_err_t err = load_from(params->sd_path, &io, out_keys, out_count);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Loaded %u keys from %s", (unsigned)*out_count, params->sd_path);
      return ESP_OK;
    }
  }

  if (params->flash_path != NULL) {
    esp_err_t err = load_from(params->flash_path, &io, out_keys, out_count);
    if (err == ESP_OK) {
      ESP_LOGI(
          TAG, "Loaded %u keys from %s (flash fallback)", (unsigned)*out_count, params->flash_path);
      return ESP_OK;
    }
  }

  ESP_LOGW(TAG,
           "No keys loaded (sd=%s flash=%s)",
           params->sd_path != NULL ? params->sd_path : "(none)",
           params->flash_path != NULL ? params->flash_path : "(none)");
  return ESP_ERR_NOT_FOUND;
}

static bool name_has_dic_extension(const char *name) {
  size_t nlen = strlen(name);
  if (nlen < DIC_EXTENSION_LEN) {
    return false;
  }
  return strcmp(name + nlen - DIC_EXTENSION_LEN, DIC_EXTENSION) == 0;
}

esp_err_t
nfc_dict_load_dir(const nfc_dict_scan_params_t *params, uint8_t *out_keys, size_t *out_count) {
  if (params == NULL || params->dir == NULL || out_keys == NULL || out_count == NULL ||
      params->key_size == 0 || params->max_keys == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  *out_count = 0;

  DIR *d = opendir(params->dir);
  if (d == NULL) {
    ESP_LOGW(TAG, "Cannot open dir %s", params->dir);
    return ESP_ERR_NOT_FOUND;
  }

  size_t prefix_len = (params->prefix != NULL) ? strlen(params->prefix) : 0;
  int files_loaded = 0;

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.') {
      continue;
    }
    if (prefix_len > 0 && strncmp(de->d_name, params->prefix, prefix_len) != 0) {
      continue;
    }
    if (!name_has_dic_extension(de->d_name)) {
      continue;
    }

    char path[MAX_PATH_LEN];
    int n = snprintf(path, sizeof(path), "%s/%s", params->dir, de->d_name);
    if (n <= 0 || (size_t)n >= sizeof(path)) {
      continue;
    }

    if (*out_count >= params->max_keys) {
      break;
    }

    uint8_t *tail = out_keys + (*out_count) * params->key_size;
    const nfc_dict_io_t file_io = {
        .key_size = params->key_size,
        .max_keys = params->max_keys - *out_count,
    };
    size_t file_count = 0;

    if (load_from(path, &file_io, tail, &file_count) != ESP_OK) {
      continue;
    }

    size_t kept = 0;
    for (size_t i = 0; i < file_count; i++) {
      const uint8_t *k = tail + i * params->key_size;
      if (nfc_dict_contains(out_keys, *out_count + kept, params->key_size, k)) {
        continue;
      }
      if (kept != i) {
        memmove(tail + kept * params->key_size, k, params->key_size);
      }
      kept++;
    }
    *out_count += kept;

    if (kept > 0) {
      ESP_LOGI(TAG, "Loaded %u new keys from %s", (unsigned)kept, de->d_name);
      files_loaded++;
    }
  }
  closedir(d);

  ESP_LOGI(TAG,
           "%s: scanned dir, %d files contributed, %u total keys",
           params->dir,
           files_loaded,
           (unsigned)*out_count);
  return ESP_OK;
}

bool nfc_dict_contains(const uint8_t *keys, size_t count, size_t key_size, const uint8_t *key) {
  if (keys == NULL || key == NULL || key_size == 0) {
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    if (memcmp(keys + i * key_size, key, key_size) == 0) {
      return true;
    }
  }
  return false;
}

esp_err_t nfc_dict_append_key(const char *sd_path, const uint8_t *key, size_t key_size) {
  if (sd_path == NULL || key == NULL || key_size == 0 || key_size > MAX_KEY_SIZE)
    return ESP_ERR_INVALID_ARG;
  FILE *file = fopen(sd_path, "a");
  if (file == NULL) {
    ESP_LOGE(TAG, "Cannot open %s for append", sd_path);
    return ESP_FAIL;
  }
  /* Isolate a previous interrupted line before appending a complete record. */
  bool failed = fputc('\n', file) == EOF;
  for (size_t i = 0; i < key_size && !failed; i++)
    failed = fprintf(file, "%02X", key[i]) < 0;
  if (!failed)
    failed = fputc('\n', file) == EOF;
  failed = failed || ferror(file) != 0;
  if (fclose(file) != 0)
    failed = true;
  return failed ? ESP_FAIL : ESP_OK;
}
