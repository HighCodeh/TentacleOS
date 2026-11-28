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

#include "doom_memory.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "info.h"
#include "sounds.h"

static const char *TAG = "DOOM_MEMORY";

state_t *states;
mobjinfo_t *mobjinfo;
sfxinfo_t *S_sfx;
musicinfo_t *S_music;

esp_err_t doom_memory_init(void) {
  if (states != NULL && mobjinfo != NULL && S_sfx != NULL && S_music != NULL)
    return ESP_OK;

  state_t *new_states = heap_caps_malloc(sizeof(doom_initial_states), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  mobjinfo_t *new_mobjinfo = heap_caps_malloc(sizeof(doom_initial_mobjinfo), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  sfxinfo_t *new_sfx = heap_caps_malloc(sizeof(doom_initial_sfx), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  musicinfo_t *new_music = heap_caps_malloc(sizeof(doom_initial_music), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (new_states == NULL || new_mobjinfo == NULL || new_sfx == NULL || new_music == NULL) {
    ESP_LOGE(TAG, "Could not allocate game tables in PSRAM");
    goto cleanup;
  }
  memcpy(new_states, doom_initial_states, sizeof(doom_initial_states));
  memcpy(new_mobjinfo, doom_initial_mobjinfo, sizeof(doom_initial_mobjinfo));
  memcpy(new_sfx, doom_initial_sfx, sizeof(doom_initial_sfx));
  memcpy(new_music, doom_initial_music, sizeof(doom_initial_music));
  for (size_t i = 0; i < NUMSFX; ++i) {
    if (new_sfx[i].link != NULL)
      new_sfx[i].link = &new_sfx[new_sfx[i].link - doom_initial_sfx];
  }
  S_sfx = new_sfx;
  S_music = new_music;
  states = new_states;
  mobjinfo = new_mobjinfo;
  return ESP_OK;

cleanup:
  free(new_states);
  free(new_mobjinfo);
  free(new_sfx);
  free(new_music);
  return ESP_ERR_NO_MEM;
}
