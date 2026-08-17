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

#include "favorites.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "FAVORITES";

#define FAV_NVS_NS  "favorites"
#define FAV_NVS_KEY "set"
#define FAV_BITS    32
#define FAV_WORDS   (((int)SCREEN_COUNT + FAV_BITS - 1) / FAV_BITS)

static uint32_t s_fav[FAV_WORDS];
static bool s_loaded = false;

static void fav_load(void) {
  if (s_loaded) {
    return;
  }
  memset(s_fav, 0, sizeof(s_fav));
  nvs_handle_t h;
  if (nvs_open(FAV_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
    size_t len = sizeof(s_fav);
    nvs_get_blob(h, FAV_NVS_KEY, s_fav, &len);
    nvs_close(h);
  }
  s_loaded = true;
}

static void fav_save(void) {
  nvs_handle_t h;
  if (nvs_open(FAV_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_blob(h, FAV_NVS_KEY, s_fav, sizeof(s_fav));
    nvs_commit(h);
    nvs_close(h);
  }
}

bool favorites_is(screen_id_t screen) {
  fav_load();
  if ((int)screen < 0 || (int)screen >= (int)SCREEN_COUNT) {
    return false;
  }
  return (s_fav[(int)screen / FAV_BITS] >> ((int)screen % FAV_BITS)) & 1u;
}

void favorites_toggle(screen_id_t screen) {
  fav_load();
  if ((int)screen < 0 || (int)screen >= (int)SCREEN_COUNT) {
    return;
  }
  s_fav[(int)screen / FAV_BITS] ^= (1u << ((int)screen % FAV_BITS));
  fav_save();
  ESP_LOGI(TAG, "toggle screen %d -> %d", (int)screen, favorites_is(screen) ? 1 : 0);
}

int favorites_count(void) {
  fav_load();
  int count = 0;
  for (int i = 0; i < FAV_WORDS; i++) {
    uint32_t w = s_fav[i];
    while (w) {
      count += (int)(w & 1u);
      w >>= 1;
    }
  }
  return count;
}
