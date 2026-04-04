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

#include "storage_write.h"
#include "storage_init.h"
#include "storage_mkdir.h"
#include "vfs_config.h"
#include "vfs_core.h"
#include "esp_log.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "storage_write";

static void resolve_path(const char *path, char *full_path, size_t size)
{
  if (strncmp(path, VFS_MOUNT_POINT, strlen(VFS_MOUNT_POINT)) == 0) {
    snprintf(full_path, size, "%s", path);
  } else if (path[0] == '/') {
    snprintf(full_path, size, "%s%s", VFS_MOUNT_POINT, path);
  } else {
    snprintf(full_path, size, "%s/%s", VFS_MOUNT_POINT, path);
  }
}

esp_err_t storage_write_string(const char *path, const char *data)
{
  if (!storage_is_mounted() || !path || !data) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[256];
  resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = storage_mkdir_recursive(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = vfs_write_file(full_path, data, strlen(data));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Write failed: %s", path);
  }

  return ret;
}

esp_err_t storage_append_string(const char *path, const char *data)
{
  if (!storage_is_mounted() || !path || !data) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[256];
  resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = storage_mkdir_recursive(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = vfs_append_file(full_path, data, strlen(data));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Append failed: %s", path);
  }

  return ret;
}

esp_err_t storage_write_binary(const char *path, const void *data, size_t size)
{
  if (!storage_is_mounted() || !path || !data) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[256];
  resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = storage_mkdir_recursive(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = vfs_write_file(full_path, data, size);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Binary write failed: %s", path);
  }

  return ret;
}

esp_err_t storage_append_binary(const char *path, const void *data, size_t size)
{
  if (!storage_is_mounted() || !path || !data) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[256];
  resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = storage_mkdir_recursive(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = vfs_append_file(full_path, data, size);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Binary append failed: %s", path);
  }

  return ret;
}

esp_err_t storage_write_line(const char *path, const char *line)
{
  if (!path || !line) {
    return ESP_ERR_INVALID_ARG;
  }

  char buffer[512];
  snprintf(buffer, sizeof(buffer), "%s\n", line);
  return storage_write_string(path, buffer);
}

esp_err_t storage_append_line(const char *path, const char *line)
{
  if (!path || !line) {
    return ESP_ERR_INVALID_ARG;
  }

  char buffer[512];
  snprintf(buffer, sizeof(buffer), "%s\n", line);
  return storage_append_string(path, buffer);
}

esp_err_t storage_write_formatted(const char *path, const char *format, ...)
{
  if (!storage_is_mounted() || !path || !format) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[256];
  resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = storage_mkdir_recursive(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  vfs_fd_t fd = vfs_open(full_path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, 0644);
  if (fd == VFS_INVALID_FD) {
    ESP_LOGE(TAG, "Open failed: %s", path);
    return ESP_FAIL;
  }

  char buffer[512];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  ssize_t written = vfs_write(fd, buffer, len);
  vfs_close(fd);

  if (written != len) {
    ESP_LOGE(TAG, "Write incomplete");
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t storage_append_formatted(const char *path, const char *format, ...)
{
  if (!storage_is_mounted() || !path || !format) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[256];
  resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = storage_mkdir_recursive(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  vfs_fd_t fd = vfs_open(full_path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND, 0644);
  if (fd == VFS_INVALID_FD) {
    ESP_LOGE(TAG, "Open failed: %s", path);
    return ESP_FAIL;
  }

  char buffer[512];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  ssize_t written = vfs_write(fd, buffer, len);
  vfs_close(fd);

  if (written != len) {
    ESP_LOGE(TAG, "Append incomplete");
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t storage_write_buffer(const char *path, const void *buffer, size_t size)
{
  return storage_write_binary(path, buffer, size);
}

esp_err_t storage_write_bytes(const char *path, const uint8_t *bytes, size_t count)
{
  return storage_write_binary(path, bytes, count);
}

esp_err_t storage_write_byte(const char *path, uint8_t byte)
{
  return storage_write_binary(path, &byte, 1);
}

esp_err_t storage_write_int(const char *path, int32_t value)
{
  return storage_write_formatted(path, "%" PRId32, value);
}

esp_err_t storage_write_float(const char *path, float value)
{
  return storage_write_formatted(path, "%.6f", value);
}

esp_err_t storage_write_csv_row(const char *path, const char **columns, size_t num_columns)
{
  if (!storage_is_mounted() || !path || !columns || num_columns == 0) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[256];
  resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = storage_mkdir_recursive(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  vfs_fd_t fd = vfs_open(full_path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, 0644);
  if (fd == VFS_INVALID_FD) {
    return ESP_FAIL;
  }

  for (size_t i = 0; i < num_columns; i++) {
    vfs_write(fd, columns[i], strlen(columns[i]));
    if (i < num_columns - 1) {
      vfs_write(fd, ",", 1);
    }
  }
  vfs_write(fd, "\n", 1);

  vfs_close(fd);
  return ESP_OK;
}

esp_err_t storage_append_csv_row(const char *path, const char **columns, size_t num_columns)
{
  if (!storage_is_mounted() || !path || !columns || num_columns == 0) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[256];
  resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = storage_mkdir_recursive(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  vfs_fd_t fd = vfs_open(full_path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND, 0644);
  if (fd == VFS_INVALID_FD) {
    return ESP_FAIL;
  }

  for (size_t i = 0; i < num_columns; i++) {
    vfs_write(fd, columns[i], strlen(columns[i]));
    if (i < num_columns - 1) {
      vfs_write(fd, ",", 1);
    }
  }
  vfs_write(fd, "\n", 1);

  vfs_close(fd);
  return ESP_OK;
}
