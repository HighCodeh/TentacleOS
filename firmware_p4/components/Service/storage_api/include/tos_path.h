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

#ifndef TOS_PATH_H
#define TOS_PATH_H

#include <stddef.h>
#include <string.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inputs must not overlap out_path. Failure leaves an empty output when writable.
static inline esp_err_t
tos_path_join(const char *base, const char *name, char *out_path, size_t capacity) {
  if (out_path == NULL || capacity == 0)
    return ESP_ERR_INVALID_ARG;
  out_path[0] = '\0';
  if (base == NULL || name == NULL)
    return ESP_ERR_INVALID_ARG;
  size_t base_len = 0;
  while (base_len < capacity && base[base_len] != '\0')
    base_len++;
  size_t name_len = 0;
  while (name_len < capacity && name[name_len] != '\0')
    name_len++;
  if (base_len >= capacity || name_len >= capacity)
    return ESP_ERR_INVALID_SIZE;
  size_t separator = base_len > 0 && base[base_len - 1] != '/' && name_len > 0 ? 1 : 0;
  if (separator >= capacity - base_len || name_len >= capacity - base_len - separator)
    return ESP_ERR_INVALID_SIZE;
  memcpy(out_path, base, base_len);
  if (separator != 0)
    out_path[base_len] = '/';
  memcpy(out_path + base_len + separator, name, name_len + 1);
  return ESP_OK;
}

#ifdef __cplusplus
}
#endif

#endif // TOS_PATH_H
