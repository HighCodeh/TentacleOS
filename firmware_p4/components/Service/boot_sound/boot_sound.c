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

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "audio_i2s.h"
#include "mp3dec.h"
#include "sys_prio.h"

static const char *TAG = "BOOT_SOUND";

#define BOOT_SOUND_PATH_MAX        128
#define BOOT_SOUND_FILE_MAX        (256 * 1024)
#define BOOT_SOUND_MAX_FRAME       2304
#define BOOT_SOUND_RATE_MIN        8000
#define BOOT_SOUND_RATE_MAX        48000
#define BOOT_SOUND_SECONDS_MAX     10
#define BOOT_SOUND_PCM_MAX         (BOOT_SOUND_RATE_MAX * BOOT_SOUND_SECONDS_MAX)
#define BOOT_SOUND_DECODE_US       (5 * 1000 * 1000)
#define BOOT_SOUND_TASK_STACK      8192
#define BOOT_SOUND_INTERNAL_MIN    (64 * 1024)
#define BOOT_SOUND_STACK_RESERVE   (BOOT_SOUND_TASK_STACK + 2048)
#define BOOT_SOUND_STEREO_CHANNELS 2

static char s_path[BOOT_SOUND_PATH_MAX];
static atomic_bool s_is_running;

static void playback_task(void *arg) {
  (void)arg;
  FILE *file = NULL;
  HMP3Decoder decoder = NULL;
  uint8_t *input = NULL;
  int16_t *frame = NULL;
  int16_t *mono = NULL;
  uint32_t rate = 0;
  size_t samples = 0;

  file = fopen(s_path, "rb");
  if (file == NULL) {
    ESP_LOGW(TAG, "Optional boot sound unavailable: %s", s_path);
    goto cleanup;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    ESP_LOGE(TAG, "Cannot seek boot sound");
    goto cleanup;
  }
  long length = ftell(file);
  if (length <= 0 || length > BOOT_SOUND_FILE_MAX || fseek(file, 0, SEEK_SET) != 0) {
    ESP_LOGE(TAG, "Invalid boot sound size");
    goto cleanup;
  }
  input = heap_caps_malloc((size_t)length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  frame =
      heap_caps_malloc(BOOT_SOUND_MAX_FRAME * sizeof(*frame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  mono = heap_caps_malloc(BOOT_SOUND_PCM_MAX * sizeof(*mono), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (input == NULL || frame == NULL || mono == NULL) {
    ESP_LOGE(TAG, "Boot sound buffer allocation failed");
    goto cleanup;
  }
  if (fread(input, 1, (size_t)length, file) != (size_t)length || ferror(file)) {
    ESP_LOGE(TAG, "Cannot read complete boot sound");
    goto cleanup;
  }
  fclose(file);
  file = NULL;
  decoder = MP3InitDecoder();
  if (decoder == NULL) {
    ESP_LOGE(TAG, "Boot sound decoder allocation failed");
    goto cleanup;
  }

  uint8_t *cursor = input;
  int remaining = (int)length;
  int64_t deadline = esp_timer_get_time() + BOOT_SOUND_DECODE_US;
  while (remaining > 0) {
    if (esp_timer_get_time() >= deadline) {
      ESP_LOGE(TAG, "Boot sound decode budget exceeded");
      goto cleanup;
    }
    int offset = MP3FindSyncWord(cursor, remaining);
    if (offset < 0) {
      break;
    }
    if (offset >= remaining) {
      ESP_LOGE(TAG, "Invalid MP3 synchronization offset");
      goto cleanup;
    }
    cursor += offset;
    remaining -= offset;
    uint8_t *before = cursor;
    int previous = remaining;
    int result = MP3Decode(decoder, &cursor, &remaining, frame, 0);
    if (remaining < 0 || remaining > previous || cursor != before + (previous - remaining)) {
      ESP_LOGE(TAG, "Invalid MP3 decoder progress");
      goto cleanup;
    }
    if (result == ERR_MP3_INDATA_UNDERFLOW) {
      break;
    }
    if (result != 0) {
      if (remaining == previous) {
        ++cursor;
        --remaining;
      }
      continue;
    }
    if (remaining == previous) {
      ESP_LOGE(TAG, "MP3 decoder made no progress");
      goto cleanup;
    }
    MP3FrameInfo info;
    MP3GetLastFrameInfo(decoder, &info);
    if ((info.nChans != 1 && info.nChans != BOOT_SOUND_STEREO_CHANNELS) ||
        info.samprate < BOOT_SOUND_RATE_MIN || info.samprate > BOOT_SOUND_RATE_MAX ||
        info.outputSamps <= 0 || info.outputSamps > BOOT_SOUND_MAX_FRAME ||
        info.outputSamps % info.nChans != 0) {
      ESP_LOGE(TAG, "Invalid MP3 frame format");
      goto cleanup;
    }
    if (rate != 0 && rate != (uint32_t)info.samprate) {
      ESP_LOGE(TAG, "Boot sound changes sample rate");
      goto cleanup;
    }
    rate = (uint32_t)info.samprate;
    size_t count = (size_t)(info.outputSamps / info.nChans);
    if (count > rate * BOOT_SOUND_SECONDS_MAX - samples) {
      ESP_LOGE(TAG, "Boot sound exceeds playback budget");
      goto cleanup;
    }
    for (size_t i = 0; i < count; ++i) {
      int32_t value = frame[i * (size_t)info.nChans];
      if (info.nChans == BOOT_SOUND_STEREO_CHANNELS) {
        value = (value + frame[i * BOOT_SOUND_STEREO_CHANNELS + 1]) / BOOT_SOUND_STEREO_CHANNELS;
      }
      mono[samples++] = (int16_t)value;
    }
    vTaskDelay(1);
  }
  if (samples != 0) {
    esp_err_t result = audio_i2s_play_pcm(mono, samples, rate);
    if (result != ESP_OK) {
      ESP_LOGW(TAG, "Boot sound playback failed: %s", esp_err_to_name(result));
    }
  }

cleanup:
  if (decoder != NULL) {
    MP3FreeDecoder(decoder);
  }
  free(mono);
  free(frame);
  free(input);
  if (file != NULL) {
    fclose(file);
  }
  atomic_store(&s_is_running, false);
  vTaskDeleteWithCaps(NULL);
}

esp_err_t boot_sound_play(const char *path) {
  if (path == NULL || path[0] != '/' || strlen(path) >= BOOT_SOUND_PATH_MAX) {
    return ESP_ERR_INVALID_ARG;
  }
  bool is_running = false;
  if (!atomic_compare_exchange_strong(&s_is_running, &is_running, true)) {
    return ESP_ERR_INVALID_STATE;
  }
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < BOOT_SOUND_INTERNAL_MIN ||
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < BOOT_SOUND_STACK_RESERVE) {
    ESP_LOGW(TAG, "Skipping optional boot sound: internal heap is low");
    atomic_store(&s_is_running, false);
    return ESP_ERR_NO_MEM;
  }
  memcpy(s_path, path, strlen(path) + 1);
  // Filesystem/flash access must not execute on an external-RAM stack.
  if (xTaskCreatePinnedToCoreWithCaps(playback_task,
                                      "boot_sound",
                                      BOOT_SOUND_TASK_STACK,
                                      NULL,
                                      SYS_PRIO_SERVICE_LO,
                                      NULL,
                                      SYS_CORE_RADIO,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create boot sound task");
    atomic_store(&s_is_running, false);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
