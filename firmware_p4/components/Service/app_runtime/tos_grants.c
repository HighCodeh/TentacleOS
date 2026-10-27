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

#include "tos_grants.h"

#include <string.h>

#include "nvs.h"

#define GRANTS_NS "tos_grants"

uint32_t tos_grants_get(const char *name) {
  if (name == NULL || name[0] == '\0')
    return 0;
  nvs_handle_t h;
  if (nvs_open(GRANTS_NS, NVS_READONLY, &h) != ESP_OK)
    return 0;
  uint32_t caps = 0;
  nvs_get_u32(h, name, &caps); // leaves caps=0 if the key is absent
  nvs_close(h);
  return caps;
}

esp_err_t tos_grants_set(const char *name, uint32_t caps) {
  if (name == NULL || name[0] == '\0' || strlen(name) > 15)
    return ESP_ERR_INVALID_ARG;
  nvs_handle_t h;
  esp_err_t e = nvs_open(GRANTS_NS, NVS_READWRITE, &h);
  if (e != ESP_OK)
    return e;
  e = nvs_set_u32(h, name, caps);
  if (e == ESP_OK)
    e = nvs_commit(h);
  nvs_close(h);
  return e;
}

esp_err_t tos_grants_clear(const char *name) {
  if (name == NULL || name[0] == '\0')
    return ESP_ERR_INVALID_ARG;
  nvs_handle_t h;
  esp_err_t e = nvs_open(GRANTS_NS, NVS_READWRITE, &h);
  if (e != ESP_OK)
    return e;
  e = nvs_erase_key(h, name);
  if (e == ESP_OK)
    e = nvs_commit(h);
  nvs_close(h);
  return (e == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : e;
}

int tos_grants_list(char names[][16], uint32_t *caps, int max) {
  nvs_iterator_t it = NULL;
  esp_err_t e = nvs_entry_find("nvs", GRANTS_NS, NVS_TYPE_U32, &it);
  int n = 0;
  while (e == ESP_OK && it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    if (n < max) {
      strlcpy(names[n], info.key, 16);
      caps[n] = tos_grants_get(info.key);
    }
    n++;
    e = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  return n;
}
