// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "storage_stream.h"
#include "storage_init.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "storage_stream";

struct storage_stream {
  FILE  *fp;
  size_t bytes_written;
  char   path[256];
};

storage_stream_t storage_stream_open(const char *path, const char *mode)
{
  if (!storage_is_mounted() || !path || !mode) {
    ESP_LOGE(TAG, "Cannot open stream: invalid args or storage not mounted");
    return NULL;
  }

  storage_stream_t stream = calloc(1, sizeof(struct storage_stream));
  if (!stream) return NULL;

  stream->fp = fopen(path, mode);
  if (!stream->fp) {
    ESP_LOGE(TAG, "Failed to open: %s", path);
    free(stream);
    return NULL;
  }

  strncpy(stream->path, path, sizeof(stream->path) - 1);
  stream->bytes_written = 0;

  ESP_LOGI(TAG, "Opened: %s", path);
  return stream;
}

esp_err_t storage_stream_write(storage_stream_t stream, const void *data, size_t size)
{
  if (!stream || !stream->fp || !data || size == 0)
    return ESP_ERR_INVALID_ARG;

  size_t written = fwrite(data, 1, size, stream->fp);
  if (written != size) {
    ESP_LOGE(TAG, "Write error: %zu/%zu bytes", written, size);
    return ESP_FAIL;
  }

  stream->bytes_written += written;
  return ESP_OK;
}

esp_err_t storage_stream_read(storage_stream_t stream, void *buf, size_t size, size_t *bytes_read)
{
  if (!stream || !stream->fp || !buf || size == 0)
    return ESP_ERR_INVALID_ARG;

  size_t read = fread(buf, 1, size, stream->fp);
  if (bytes_read) *bytes_read = read;

  return (read > 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t storage_stream_flush(storage_stream_t stream)
{
  if (!stream || !stream->fp)
    return ESP_ERR_INVALID_ARG;

  return (fflush(stream->fp) == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t storage_stream_close(storage_stream_t stream)
{
  if (!stream) return ESP_ERR_INVALID_ARG;

  esp_err_t ret = ESP_OK;
  if (stream->fp) {
    if (fclose(stream->fp) != 0)
      ret = ESP_FAIL;
  }

  ESP_LOGI(TAG, "Closed: %s (%zu bytes written)", stream->path, stream->bytes_written);
  free(stream);
  return ret;
}

bool storage_stream_is_open(storage_stream_t stream)
{
  return (stream && stream->fp);
}

size_t storage_stream_bytes_written(storage_stream_t stream)
{
  return stream ? stream->bytes_written : 0;
}
