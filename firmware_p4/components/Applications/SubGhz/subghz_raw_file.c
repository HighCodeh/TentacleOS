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

#include "subghz_raw_file.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "storage_mkdir.h"
#include "subghz_protocol_serializer.h"
#include "subghz_storage.h"
#include "tos_storage_paths.h"

static const char *TAG = "SUBGHZ_RAW_FILE";

#define RAW_PATH_SIZE       160
#define RAW_HEADER_SIZE     1024
#define RAW_TEMP_ATTEMPTS   1024
#define RAW_BATCH_PULSES    512
#define RAW_PULSE_TEXT_SIZE 12
#define RAW_LINE_OVERHEAD   16
#define RAW_LINE_SIZE       (RAW_BATCH_PULSES * RAW_PULSE_TEXT_SIZE + RAW_LINE_OVERHEAD)
#define RAW_FILE_MODE       0600
#define RAW_RADIX           10
struct subghz_raw_file {
  atomic_uint references;
  char path[RAW_PATH_SIZE];
  FILE *writer;
  char *line;
  bool is_temporary;
  bool is_ready;
  uint32_t frequency;
  cc1101_radio_config_t radio;
  uint64_t count;
};
static atomic_uint s_temp_sequence;

static bool valid_name(const char *name) {
  return name && name[0] && name[0] != '.' && strlen(name) < SUBGHZ_STORAGE_NAME_MAX &&
         strpbrk(name, "/\\") == NULL;
}

static subghz_raw_file_t *allocate_file(void) {
  subghz_raw_file_t *file = heap_caps_malloc(sizeof(*file), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (file == NULL) {
    ESP_LOGE(TAG, "Cannot allocate RAW file state");
    return NULL;
  }
  memset(file, 0, sizeof(*file));
  atomic_init(&file->references, 1);
  return file;
}

esp_err_t subghz_raw_file_create(uint32_t frequency,
                                 const cc1101_radio_config_t *radio,
                                 subghz_raw_file_t **out_file) {
  if (out_file == NULL || cc1101_config_validate(radio) != ESP_OK)
    return ESP_ERR_INVALID_ARG;
  *out_file = NULL;
  esp_err_t error = storage_mkdir_recursive(TOS_PATH_SUBGHZ);
  if (error != ESP_OK)
    return error;
  subghz_raw_file_t *file = allocate_file();
  if (file == NULL)
    return ESP_ERR_NO_MEM;
  file->line = heap_caps_malloc(RAW_LINE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (file->line == NULL) {
    ESP_LOGE(TAG, "Cannot allocate RAW write batch");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  int fd = -1;
  for (unsigned attempt = 0; attempt < RAW_TEMP_ATTEMPTS; ++attempt) {
    unsigned sequence = atomic_fetch_add(&s_temp_sequence, 1);
    snprintf(file->path, sizeof(file->path), TOS_PATH_SUBGHZ "/.raw-%08x.tmp", sequence);
    fd = open(file->path, O_CREAT | O_EXCL | O_WRONLY, RAW_FILE_MODE);
    if (fd >= 0 || errno != EEXIST)
      break;
  }
  if (fd < 0) {
    error = ESP_FAIL;
    goto cleanup;
  }
  file->is_temporary = true;
  file->writer = fdopen(fd, "wb");
  if (file->writer == NULL) {
    close(fd);
    error = ESP_FAIL;
    goto cleanup;
  }
  file->frequency = frequency;
  file->radio = *radio;
  if (fprintf(file->writer,
              "Filetype: High Boy SubGhz File\nVersion: 1\nFrequency: %" PRIu32
              "\nPreset: %u\nRadio: %u %u %u %u\nProtocol: RAW\n",
              frequency,
              radio->preset,
              radio->mdmcfg4,
              radio->mdmcfg3,
              radio->deviatn,
              radio->agcctrl2) < 0) {
    error = ESP_FAIL;
    goto cleanup;
  }
  *out_file = file;
  return ESP_OK;
cleanup:
  subghz_raw_file_release(file);
  return error;
}

esp_err_t subghz_raw_file_append(subghz_raw_file_t *file, const int32_t *pulses, size_t count) {
  if (file == NULL || file->writer == NULL || pulses == NULL)
    return ESP_ERR_INVALID_STATE;
  while (count) {
    size_t batch = count > RAW_BATCH_PULSES ? RAW_BATCH_PULSES : count;
    size_t used = (size_t)snprintf(file->line, RAW_LINE_SIZE, "RAW_Data:");
    for (size_t i = 0; i < batch; ++i) {
      if (pulses[i] == 0 || pulses[i] == INT32_MIN)
        return ESP_ERR_INVALID_ARG;
      int length = snprintf(file->line + used, RAW_LINE_SIZE - used, " %" PRId32, pulses[i]);
      if (length <= 0 || (size_t)length >= RAW_LINE_SIZE - used - 1)
        return ESP_ERR_INVALID_SIZE;
      used += (size_t)length;
    }
    file->line[used++] = '\n';
    if (fwrite(file->line, 1, used, file->writer) != used)
      return ESP_FAIL;
    file->count += batch;
    pulses += batch;
    count -= batch;
  }
  return ESP_OK;
}

esp_err_t subghz_raw_file_finish(subghz_raw_file_t *file) {
  if (file == NULL || file->writer == NULL)
    return ESP_ERR_INVALID_STATE;
  bool has_failed = ferror(file->writer) != 0;
  int closed = fclose(file->writer);
  file->writer = NULL;
  free(file->line);
  file->line = NULL;
  file->is_ready = !has_failed && closed == 0 && file->count != 0;
  return file->is_ready ? ESP_OK : ESP_FAIL;
}

esp_err_t subghz_raw_file_save(subghz_raw_file_t *file, const char *name) {
  if (!valid_name(name))
    return ESP_ERR_INVALID_ARG;
  if (file == NULL || !file->is_ready || !file->is_temporary)
    return ESP_ERR_INVALID_STATE;
  char path[RAW_PATH_SIZE];
  snprintf(path, sizeof(path), TOS_PATH_SUBGHZ "/%s.sub", name);
  struct stat info;
  if (stat(path, &info) == 0)
    return ESP_ERR_INVALID_STATE;
  if (errno != ENOENT || rename(file->path, path) != 0)
    return ESP_FAIL;
  snprintf(file->path, sizeof(file->path), "%s", path);
  file->is_temporary = false;
  return ESP_OK;
}

void subghz_raw_file_acquire(subghz_raw_file_t *file) {
  if (file)
    atomic_fetch_add(&file->references, 1);
}
void subghz_raw_file_release(subghz_raw_file_t *file) {
  if (file == NULL || atomic_fetch_sub(&file->references, 1) != 1)
    return;
  if (file->writer)
    fclose(file->writer);
  if (file->is_temporary && remove(file->path) != 0)
    ESP_LOGW(TAG, "Cannot remove temporary RAW capture");
  free(file->line);
  free(file);
}
void subghz_raw_file_info(const subghz_raw_file_t *file,
                          uint32_t *out_frequency,
                          cc1101_radio_config_t *out_radio,
                          uint64_t *out_count) {
  *out_frequency = file->frequency;
  *out_radio = file->radio;
  *out_count = file->count;
}

esp_err_t subghz_raw_reader_rewind(subghz_raw_reader_t *reader) {
  if (reader == NULL || reader->file == NULL || fseek(reader->file, 0, SEEK_SET) != 0)
    return ESP_FAIL;
  char line[RAW_HEADER_SIZE];
  size_t bytes = 0;
  while (bytes < RAW_HEADER_SIZE) {
    long position = ftell(reader->file);
    if (position < 0 || fgets(line, sizeof(line), reader->file) == NULL)
      return ESP_FAIL;
    if (strncmp(line, "RAW_Data:", strlen("RAW_Data:")) == 0) {
      if (fseek(reader->file, position, SEEK_SET) != 0)
        return ESP_FAIL;
      reader->is_in_data = false;
      reader->has_samples = false;
      return ESP_OK;
    }
    bytes += strlen(line);
  }
  return ESP_ERR_INVALID_SIZE;
}

esp_err_t subghz_raw_reader_open(const subghz_raw_file_t *file, subghz_raw_reader_t *out_reader) {
  if (!file || !file->is_ready || !out_reader)
    return ESP_ERR_INVALID_STATE;
  *out_reader = (subghz_raw_reader_t){.file = fopen(file->path, "rb")};
  if (!out_reader->file)
    return ESP_FAIL;
  esp_err_t error = subghz_raw_reader_rewind(out_reader);
  if (error != ESP_OK)
    subghz_raw_reader_close(out_reader);
  return error;
}

esp_err_t subghz_raw_reader_next(subghz_raw_reader_t *reader, int32_t *out_pulse, bool *out_eof) {
  if (!reader || !reader->file || !out_pulse || !out_eof)
    return ESP_ERR_INVALID_ARG;
  *out_eof = false;
  int ch;
  while (true) {
    ch = fgetc(reader->file);
    if (ch == EOF) {
      if (ferror(reader->file) || !reader->has_samples)
        return ESP_FAIL;
      *out_eof = true;
      return ESP_OK;
    }
    if (ch == '\n') {
      reader->is_in_data = false;
      continue;
    }
    if (ch == '\r' || ch == ' ' || ch == '\t')
      continue;
    if (!reader->is_in_data) {
      if (ch == '#') {
        while ((ch = fgetc(reader->file)) != EOF && ch != '\n') {}
        if (ferror(reader->file))
          return ESP_FAIL;
        continue;
      }
      const char *prefix = "RAW_Data:";
      for (size_t i = 0; prefix[i]; ++i) {
        if (ch != prefix[i])
          return ESP_ERR_INVALID_ARG;
        if (prefix[i + 1])
          ch = fgetc(reader->file);
      }
      reader->is_in_data = true;
      continue;
    }
    break;
  }
  bool is_negative = ch == '-';
  if (ch == '-' || ch == '+')
    ch = fgetc(reader->file);
  if (ch < '0' || ch > '9')
    return ESP_ERR_INVALID_ARG;
  uint32_t value = 0;
  do {
    unsigned digit = ch - '0';
    if (value > (INT32_MAX - digit) / RAW_RADIX)
      return ESP_ERR_INVALID_SIZE;
    value = value * RAW_RADIX + digit;
    ch = fgetc(reader->file);
  } while (ch >= '0' && ch <= '9');
  if (value == 0 || (ch != EOF && !isspace((unsigned char)ch)))
    return ESP_ERR_INVALID_ARG;
  if (ch == '\n')
    reader->is_in_data = false;
  if (ch == EOF && ferror(reader->file))
    return ESP_FAIL;
  *out_pulse = is_negative ? -(int32_t)value : (int32_t)value;
  reader->has_samples = true;
  return ESP_OK;
}
void subghz_raw_reader_close(subghz_raw_reader_t *reader) {
  if (reader && reader->file) {
    fclose(reader->file);
    *reader = (subghz_raw_reader_t){0};
  }
}

esp_err_t subghz_raw_file_load(const char *name, subghz_raw_file_t **out_file) {
  if (!valid_name(name) || !out_file)
    return ESP_ERR_INVALID_ARG;
  *out_file = NULL;
  subghz_raw_file_t *file = allocate_file();
  if (!file)
    return ESP_ERR_NO_MEM;
  snprintf(file->path, sizeof(file->path), TOS_PATH_SUBGHZ "/%s.sub", name);
  FILE *input = fopen(file->path, "rb");
  esp_err_t error = ESP_ERR_NOT_FOUND;
  if (!input)
    goto cleanup;
  char header[RAW_HEADER_SIZE] = {0};
  size_t used = 0;
  error = ESP_ERR_INVALID_SIZE;
  while (used < sizeof(header) - 1) {
    if (!fgets(header + used, sizeof(header) - used, input)) {
      error = ESP_FAIL;
      break;
    }
    if (strncmp(header + used, "RAW_Data:", strlen("RAW_Data:")) == 0) {
      snprintf(header + used, sizeof(header) - used, "RAW_Data: 1 -1\n");
      int32_t pulses[2];
      size_t count;
      uint8_t preset;
      if (!strstr(header, "Protocol: RAW\n") && !strstr(header, "Protocol: RAW\r\n")) {
        error = ESP_ERR_NOT_SUPPORTED;
        break;
      }
      error = subghz_protocol_parse_capture(header, pulses, 2, &count, &file->frequency, &preset);
      if (error == ESP_OK)
        error = subghz_protocol_parse_radio(header, preset, &file->radio);
      file->is_ready = error == ESP_OK;
      break;
    }
    if (strstr(header + used, "Protocol:") && !strstr(header + used, "Protocol: RAW")) {
      error = ESP_ERR_NOT_SUPPORTED;
      break;
    }
    used += strlen(header + used);
  }
  if (fclose(input) != 0)
    error = ESP_FAIL;

cleanup:
  if (error == ESP_OK)
    *out_file = file;
  else
    subghz_raw_file_release(file);
  return error;
}
