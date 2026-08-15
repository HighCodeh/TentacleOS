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

#include "storage_atomic.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"

static const char *TAG = "STORAGE_ATOMIC";

#define TMP_SUFFIX   ".tmp"
#define TMP_PATH_MAX 256

esp_err_t storage_write_atomic(const char *path, const void *data, size_t len) {
  if (path == NULL || (data == NULL && len > 0))
    return ESP_ERR_INVALID_ARG;

  char tmp[TMP_PATH_MAX];
  int n = snprintf(tmp, sizeof(tmp), "%s%s", path, TMP_SUFFIX);
  if (n <= 0 || (size_t)n >= sizeof(tmp))
    return ESP_ERR_INVALID_SIZE;

  FILE *f = fopen(tmp, "wb");
  if (f == NULL) {
    ESP_LOGE(TAG, "open %s failed: %s", tmp, strerror(errno));
    return ESP_FAIL;
  }

  bool ok = (len == 0) || (fwrite(data, 1, len, f) == len);
  if (ok)
    ok = (fflush(f) == 0);
  if (ok)
    ok = (fsync(fileno(f)) == 0);
  int close_ret = fclose(f);
  if (ok)
    ok = (close_ret == 0);

  if (!ok) {
    ESP_LOGE(TAG, "write %s failed: %s", tmp, strerror(errno));
    remove(tmp);
    return ESP_FAIL;
  }

  if (rename(tmp, path) == 0)
    return ESP_OK;

  if (errno == EEXIST) {
    if (remove(path) != 0) {
      ESP_LOGE(TAG, "unlink %s failed: %s", path, strerror(errno));
      remove(tmp);
      return ESP_FAIL;
    }
    if (rename(tmp, path) == 0)
      return ESP_OK;
    ESP_LOGE(TAG, "rename %s failed after unlink: %s", tmp, strerror(errno));
    return ESP_FAIL;
  }

  ESP_LOGE(TAG, "rename %s -> %s failed: %s", tmp, path, strerror(errno));
  remove(tmp);
  return ESP_FAIL;
}
