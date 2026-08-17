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

#include "wifi_names.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "WIFI_NAMES";
#define NVS_NS  "wifi_names"
#define NVS_KEY "list"

typedef struct {
  int count;
  char names[WIFI_NAMES_MAX][WIFI_NAME_LEN + 1];
} store_t;

static store_t s_store;
static bool s_loaded = false;

static const char *DEFAULTS[] = {
    "NET_VIVO_2.4G",
    "VIVOFIBRA-5521",
    "CLARO_WIFI_3A",
    "TP-Link_4F2A",
    "Office-Guest",
    "Familia Souza",
};
#define DEFAULTS_N ((int)(sizeof(DEFAULTS) / sizeof(DEFAULTS[0])))

static void seed_defaults(void) {
  s_store.count = 0;
  for (int i = 0; i < DEFAULTS_N && i < WIFI_NAMES_MAX; i++) {
    strncpy(s_store.names[s_store.count], DEFAULTS[i], WIFI_NAME_LEN);
    s_store.names[s_store.count][WIFI_NAME_LEN] = '\0';
    s_store.count++;
  }
}

static void persist(void) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK)
    return;
  nvs_set_blob(h, NVS_KEY, &s_store, sizeof(s_store));
  nvs_commit(h);
  nvs_close(h);
}

void wifi_names_init(void) {
  if (s_loaded)
    return;
  s_loaded = true;
  nvs_handle_t h;
  size_t len = sizeof(s_store);
  if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
    esp_err_t r = nvs_get_blob(h, NVS_KEY, &s_store, &len);
    nvs_close(h);
    if (r == ESP_OK && len == sizeof(s_store) && s_store.count >= 0 &&
        s_store.count <= WIFI_NAMES_MAX) {
      ESP_LOGI(TAG, "loaded %d names", s_store.count);
      return;
    }
  }
  seed_defaults();
  persist();
  ESP_LOGI(TAG, "seeded %d default names", s_store.count);
}

int wifi_names_count(void) {
  wifi_names_init();
  return s_store.count;
}

const char *wifi_names_get(int index) {
  wifi_names_init();
  if (index < 0 || index >= s_store.count)
    return NULL;
  return s_store.names[index];
}

bool wifi_names_add(const char *name) {
  wifi_names_init();
  if (name == NULL || name[0] == '\0' || s_store.count >= WIFI_NAMES_MAX)
    return false;
  strncpy(s_store.names[s_store.count], name, WIFI_NAME_LEN);
  s_store.names[s_store.count][WIFI_NAME_LEN] = '\0';
  s_store.count++;
  persist();
  return true;
}

void wifi_names_set(int index, const char *name) {
  wifi_names_init();
  if (index < 0 || index >= s_store.count || name == NULL || name[0] == '\0')
    return;
  strncpy(s_store.names[index], name, WIFI_NAME_LEN);
  s_store.names[index][WIFI_NAME_LEN] = '\0';
  persist();
}

void wifi_names_remove(int index) {
  wifi_names_init();
  if (index < 0 || index >= s_store.count)
    return;
  for (int i = index; i < s_store.count - 1; i++)
    memcpy(s_store.names[i], s_store.names[i + 1], WIFI_NAME_LEN + 1);
  s_store.count--;
  persist();
}
