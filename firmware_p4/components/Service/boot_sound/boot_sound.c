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

#include "boot_sound.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "audio_i2s.h"
#include "mp3dec.h"
#include "sys_prio.h"

static const char *TAG = "BOOT_SOUND";

#define BOOT_SOUND_PATH_MAX      128
#define BOOT_SOUND_READBUF       4096
#define BOOT_SOUND_MAX_FRAME     2304
#define BOOT_SOUND_REFILL_MIN    1024
#define BOOT_SOUND_TASK_STACK    8192
#define BOOT_SOUND_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define BOOT_SOUND_TASK_CORE     SYS_CORE_RADIO

static char s_path[BOOT_SOUND_PATH_MAX];
static volatile bool s_running = false;
static TaskHandle_t s_task = NULL;

static void boot_sound_task(void *arg) {
  (void)arg;

  FILE *f = fopen(s_path, "rb");
  HMP3Decoder dec = NULL;
  uint8_t *readbuf = heap_caps_malloc(BOOT_SOUND_READBUF, MALLOC_CAP_SPIRAM);
  int16_t *pcm = heap_caps_malloc(BOOT_SOUND_MAX_FRAME * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  int16_t *mono =
      heap_caps_malloc((BOOT_SOUND_MAX_FRAME / 2 + 4) * sizeof(int16_t), MALLOC_CAP_SPIRAM);

  if (f != NULL)
    dec = MP3InitDecoder();

  if (f == NULL || dec == NULL || readbuf == NULL || pcm == NULL || mono == NULL) {
    ESP_LOGE(TAG, "init failed for %s", s_path);
    if (dec != NULL)
      MP3FreeDecoder(dec);
    free(readbuf);
    free(pcm);
    free(mono);
    if (f != NULL)
      fclose(f);
    s_running = false;
    s_task = NULL;
    vTaskDeleteWithCaps(NULL);
    return;
  }

  uint8_t *rp = readbuf;
  int left = 0;
  bool started = false;
  bool eof = false;

  while (true) {
    if (left < BOOT_SOUND_REFILL_MIN) {
      if (rp != readbuf && left > 0)
        memmove(readbuf, rp, (size_t)left);
      rp = readbuf;
      size_t got = fread(readbuf + left, 1, (size_t)(BOOT_SOUND_READBUF - left), f);
      left += (int)got;
      if (got == 0)
        eof = true;
      if (eof && left < 4)
        break;
    }

    int off = MP3FindSyncWord(rp, left);
    if (off < 0) {
      if (eof)
        break;
      left = 0;
      continue;
    }
    rp += off;
    left -= off;

    int err = MP3Decode(dec, &rp, &left, pcm, 0);
    if (err != 0) {
      if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
        if (eof)
          break;
        if (left > 0) {
          memmove(readbuf, rp, (size_t)left);
          rp = readbuf;
        }
        continue;
      }
      if (left > 0) {
        rp++;
        left--;
      }
      continue;
    }

    MP3FrameInfo fi;
    MP3GetLastFrameInfo(dec, &fi);
    if (fi.samprate <= 0 || fi.nChans <= 0 || fi.outputSamps <= 0)
      continue;

    if (!started) {
      if (audio_i2s_stream_start((uint32_t)fi.samprate) != ESP_OK) {
        ESP_LOGE(TAG, "stream start failed");
        break;
      }
      started = true;
    }

    int frames = fi.outputSamps / fi.nChans;
    if (frames > BOOT_SOUND_MAX_FRAME / 2)
      frames = BOOT_SOUND_MAX_FRAME / 2;

    if (fi.nChans == 2) {
      for (int i = 0; i < frames; i++) {
        int l = pcm[2 * i];
        int r = pcm[2 * i + 1];
        mono[i] = (int16_t)((l + r) / 2);
      }
    } else {
      for (int i = 0; i < frames; i++)
        mono[i] = pcm[i];
    }

    audio_i2s_stream_write(mono, frames);
  }

  if (started)
    audio_i2s_stream_stop();
  MP3FreeDecoder(dec);
  free(readbuf);
  free(pcm);
  free(mono);
  fclose(f);

  ESP_LOGI(TAG, "finished");
  s_running = false;
  s_task = NULL;
  vTaskDeleteWithCaps(NULL);
}

esp_err_t boot_sound_play(const char *path) {
  if (path == NULL)
    return ESP_ERR_INVALID_ARG;
  if (s_running)
    return ESP_ERR_INVALID_STATE;

  strlcpy(s_path, path, sizeof(s_path));
  s_running = true;

  // MP3 decode is cache-safe task work, so its temporary 8 KB stack belongs in
  // PSRAM. Keeping it internal caused the low-memory supervisor to fire during
  // the boot jingle even though tens of megabytes of PSRAM were idle.
  BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(boot_sound_task,
                                                   "boot_sound",
                                                   BOOT_SOUND_TASK_STACK,
                                                   NULL,
                                                   BOOT_SOUND_TASK_PRIORITY,
                                                   &s_task,
                                                   BOOT_SOUND_TASK_CORE,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ret != pdPASS) {
    s_running = false;
    ESP_LOGE(TAG, "failed to create boot sound task");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}
