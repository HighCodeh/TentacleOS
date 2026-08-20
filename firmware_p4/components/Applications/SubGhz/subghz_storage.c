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

#include "subghz_storage.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "storage_mkdir.h"
#include "subghz_protocol_serializer.h"
#include "tos_storage_paths.h"

static const char *TAG = "SUBGHZ_STORAGE";

#define DECODED_BUF_SIZE 1024
#define RAW_BUF_SIZE     8192
#define STORAGE_DIR      TOS_PATH_SUBGHZ
#define PATH_BUF_SIZE    128
#define HEADER_PEEK_SIZE 256
#define SUB_EXT          ".sub"

static void build_path(char *out, size_t out_size, const char *name) {
  snprintf(out, out_size, "%s/%s%s", STORAGE_DIR, name, SUB_EXT);
}

static esp_err_t write_file(const char *name, const char *content, size_t len) {
  if (name == NULL || content == NULL || len == 0)
    return ESP_ERR_INVALID_ARG;

  storage_mkdir_recursive(STORAGE_DIR);

  char path[PATH_BUF_SIZE];
  build_path(path, sizeof(path), name);

  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    ESP_LOGE(TAG, "open for write failed: %s", path);
    return ESP_FAIL;
  }

  size_t written = fwrite(content, 1, len, f);
  fclose(f);

  if (written != len) {
    ESP_LOGE(TAG, "short write: %s (%u/%u)", path, (unsigned)written, (unsigned)len);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "saved %s (%u bytes)", path, (unsigned)len);
  return ESP_OK;
}

esp_err_t subghz_storage_init(void) {
  esp_err_t err = storage_mkdir_recursive(STORAGE_DIR);
  ESP_LOGI(TAG, "storage ready at %s", STORAGE_DIR);
  return err;
}

esp_err_t subghz_storage_save_decoded(const char *name,
                                      const subghz_data_t *data,
                                      uint32_t frequency,
                                      uint32_t te) {
  if (name == NULL || data == NULL) {
    ESP_LOGE(TAG, "Invalid arguments to save_decoded");
    return ESP_ERR_INVALID_ARG;
  }

  char *buf = malloc(DECODED_BUF_SIZE);
  if (buf == NULL)
    return ESP_ERR_NO_MEM;

  size_t len = subghz_protocol_serialize_decoded(data, frequency, te, buf, DECODED_BUF_SIZE);
  esp_err_t err = write_file(name, buf, len);

  free(buf);
  return err;
}

esp_err_t
subghz_storage_save_raw(const char *name, const int32_t *pulses, size_t count, uint32_t frequency) {
  if (name == NULL || pulses == NULL || count == 0) {
    ESP_LOGE(TAG, "Invalid arguments to save_raw");
    return ESP_ERR_INVALID_ARG;
  }

  char *buf = malloc(RAW_BUF_SIZE);
  if (buf == NULL)
    return ESP_ERR_NO_MEM;

  size_t len = subghz_protocol_serialize_raw(pulses, count, frequency, buf, RAW_BUF_SIZE);
  esp_err_t err = write_file(name, buf, len);

  free(buf);
  return err;
}

int subghz_storage_read(const char *name, char *out_buf, size_t out_size) {
  if (name == NULL || out_buf == NULL || out_size == 0)
    return -1;

  char path[PATH_BUF_SIZE];
  build_path(path, sizeof(path), name);

  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return -1;

  size_t rd = fread(out_buf, 1, out_size - 1, f);
  fclose(f);
  out_buf[rd] = '\0';
  return (int)rd;
}

static void parse_header(const char *content,
                         char *proto_out,
                         size_t proto_size,
                         uint32_t *freq_out) {
  proto_out[0] = '\0';
  *freq_out = 0;

  const char *fp = strstr(content, "Frequency: ");
  if (fp != NULL)
    *freq_out = (uint32_t)strtoul(fp + strlen("Frequency: "), NULL, 10);

  const char *pp = strstr(content, "Protocol: ");
  if (pp != NULL) {
    pp += strlen("Protocol: ");
    size_t i = 0;
    while (pp[i] != '\0' && pp[i] != '\n' && pp[i] != '\r' && i < proto_size - 1) {
      proto_out[i] = pp[i];
      i++;
    }
    proto_out[i] = '\0';
  }
}

int subghz_storage_list(subghz_storage_entry_t *out, int max_entries) {
  if (out == NULL || max_entries <= 0)
    return -1;

  DIR *d = opendir(STORAGE_DIR);
  if (d == NULL)
    return 0;

  int count = 0;
  struct dirent *ent;
  char content[HEADER_PEEK_SIZE];

  while ((ent = readdir(d)) != NULL && count < max_entries) {
    if (ent->d_name[0] == '.')
      continue;
    const char *dot = strrchr(ent->d_name, '.');
    if (dot == NULL || strcmp(dot, SUB_EXT) != 0)
      continue;

    size_t base_len = (size_t)(dot - ent->d_name);
    if (base_len == 0 || base_len >= SUBGHZ_STORAGE_NAME_MAX)
      continue;

    subghz_storage_entry_t *e = &out[count];
    memcpy(e->name, ent->d_name, base_len);
    e->name[base_len] = '\0';
    e->protocol[0] = '\0';
    e->frequency = 0;

    int rd = subghz_storage_read(e->name, content, sizeof(content));
    if (rd > 0)
      parse_header(content, e->protocol, sizeof(e->protocol), &e->frequency);
    if (e->protocol[0] == '\0')
      strlcpy(e->protocol, "RAW", sizeof(e->protocol));

    count++;
  }

  closedir(d);
  return count;
}

esp_err_t subghz_storage_delete(const char *name) {
  if (name == NULL)
    return ESP_ERR_INVALID_ARG;

  char path[PATH_BUF_SIZE];
  build_path(path, sizeof(path), name);

  if (remove(path) != 0) {
    ESP_LOGE(TAG, "delete failed: %s", path);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "deleted %s", path);
  return ESP_OK;
}
