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

#include "storage_write.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"

#include "storage_init.h"
#include "storage_internal.h"
#include "storage_mkdir.h"
#include "vfs_core.h"

static const char *TAG = "STORAGE_WRITE";

#define FORMAT_BUF_SIZE 512

static esp_err_t ensure_parent_dir(const char *full_path) {
  char dir[VFS_MAX_PATH];
  strncpy(dir, full_path, sizeof(dir) - 1);
  dir[sizeof(dir) - 1] = '\0';

  char *slash = strrchr(dir, '/');
  if (slash == NULL || slash == dir) {
    return ESP_OK;
  }
  *slash = '\0';

  esp_err_t ret = storage_mkdir_recursive(dir);
  if (ret != ESP_OK) {
    return ret;
  }

  // An earlier bug created directories where files should be. Remove such a
  // stale directory so the write can open the path as a file.
  struct stat st;
  if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
    ESP_LOGW(TAG, "Removing stale directory in place of file: %s", full_path);
    if (rmdir(full_path) != 0) {
      ESP_LOGE(TAG, "Failed to remove stale directory: %s", full_path);
      return ESP_FAIL;
    }
  }

  return ESP_OK;
}

esp_err_t storage_write_string(const char *path, const char *data) {
  if (!storage_is_mounted() || path == NULL || data == NULL) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[VFS_MAX_PATH];
  storage_resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = ensure_parent_dir(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = vfs_write_file(full_path, data, strlen(data));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Write failed: %s", path);
  }

  return ret;
}

esp_err_t storage_append_string(const char *path, const char *data) {
  if (!storage_is_mounted() || path == NULL || data == NULL) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[VFS_MAX_PATH];
  storage_resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = ensure_parent_dir(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = vfs_append_file(full_path, data, strlen(data));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Append failed: %s", path);
  }

  return ret;
}

esp_err_t storage_write_binary(const char *path, const void *data, size_t size) {
  if (!storage_is_mounted() || path == NULL || data == NULL) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[VFS_MAX_PATH];
  storage_resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = ensure_parent_dir(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = vfs_write_file(full_path, data, size);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Binary write failed: %s", path);
  }

  return ret;
}

esp_err_t storage_append_binary(const char *path, const void *data, size_t size) {
  if (!storage_is_mounted() || path == NULL || data == NULL) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[VFS_MAX_PATH];
  storage_resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = ensure_parent_dir(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = vfs_append_file(full_path, data, size);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Binary append failed: %s", path);
  }

  return ret;
}

esp_err_t storage_write_line(const char *path, const char *line) {
  if (path == NULL || line == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  char buffer[FORMAT_BUF_SIZE];
  snprintf(buffer, sizeof(buffer), "%s\n", line);
  return storage_write_string(path, buffer);
}

esp_err_t storage_append_line(const char *path, const char *line) {
  if (path == NULL || line == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  char buffer[FORMAT_BUF_SIZE];
  snprintf(buffer, sizeof(buffer), "%s\n", line);
  return storage_append_string(path, buffer);
}

esp_err_t storage_write_formatted(const char *path, const char *format, ...) {
  if (!storage_is_mounted() || path == NULL || format == NULL) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[VFS_MAX_PATH];
  storage_resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = ensure_parent_dir(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  vfs_fd_t fd = vfs_open(full_path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, 0644);
  if (fd == VFS_INVALID_FD) {
    ESP_LOGE(TAG, "Open failed: %s", path);
    return ESP_FAIL;
  }

  char buffer[FORMAT_BUF_SIZE];
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

esp_err_t storage_append_formatted(const char *path, const char *format, ...) {
  if (!storage_is_mounted() || path == NULL || format == NULL) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[VFS_MAX_PATH];
  storage_resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = ensure_parent_dir(full_path);
  if (ret != ESP_OK) {
    return ret;
  }

  vfs_fd_t fd = vfs_open(full_path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND, 0644);
  if (fd == VFS_INVALID_FD) {
    ESP_LOGE(TAG, "Open failed: %s", path);
    return ESP_FAIL;
  }

  char buffer[FORMAT_BUF_SIZE];
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

esp_err_t storage_write_buffer(const char *path, const void *buffer, size_t size) {
  return storage_write_binary(path, buffer, size);
}

esp_err_t storage_write_bytes(const char *path, const uint8_t *bytes, size_t count) {
  return storage_write_binary(path, bytes, count);
}

esp_err_t storage_write_byte(const char *path, uint8_t byte) {
  return storage_write_binary(path, &byte, 1);
}

esp_err_t storage_write_int(const char *path, int32_t value) {
  return storage_write_formatted(path, "%" PRId32, value);
}

esp_err_t storage_write_float(const char *path, float value) {
  return storage_write_formatted(path, "%.6f", value);
}

esp_err_t storage_write_csv_row(const char *path, const char **columns, size_t num_columns) {
  if (!storage_is_mounted() || path == NULL || columns == NULL || num_columns == 0) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[VFS_MAX_PATH];
  storage_resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = ensure_parent_dir(full_path);
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

esp_err_t storage_append_csv_row(const char *path, const char **columns, size_t num_columns) {
  if (!storage_is_mounted() || path == NULL || columns == NULL || num_columns == 0) {
    return !storage_is_mounted() ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG;
  }

  char full_path[VFS_MAX_PATH];
  storage_resolve_path(path, full_path, sizeof(full_path));

  esp_err_t ret = ensure_parent_dir(full_path);
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
