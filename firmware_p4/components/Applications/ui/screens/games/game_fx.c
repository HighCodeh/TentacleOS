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

#include "game_fx.h"

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_i2s.h"
#include "drv2605l.h"

#define FX_AMP            0.42f
#define SND_TASK_STACK    4096
#define SND_TASK_PRIORITY 4

static const audio_note_t SND_FLAP[] = {{780, 28}};
static const audio_note_t SND_SCORE[] = {{1568, 45}, {2093, 70}};
static const audio_note_t SND_EAT[] = {{1318, 35}, {1760, 45}};
static const audio_note_t SND_BOUNCE[] = {{1046, 24}};
static const audio_note_t SND_CRASH[] = {{330, 110}, {196, 180}};
static const audio_note_t SND_START[] = {{1046, 55}, {1318, 55}, {1568, 85}};

typedef struct {
  const audio_note_t *notes;
  int count;
  uint8_t effect;
} cue_t;

static cue_t cue_for(game_fx_t k) {
  switch (k) {
    case GFX_FLAP:
      return (cue_t){SND_FLAP, 1, 7};
    case GFX_SCORE:
      return (cue_t){SND_SCORE, 2, 10};
    case GFX_EAT:
      return (cue_t){SND_EAT, 2, 1};
    case GFX_BOUNCE:
      return (cue_t){SND_BOUNCE, 1, 5};
    case GFX_CRASH:
      return (cue_t){SND_CRASH, 2, 16};
    case GFX_START:
      return (cue_t){SND_START, 3, 4};
    default:
      return (cue_t){SND_FLAP, 1, 7};
  }
}

static volatile bool s_snd_busy = false;
static const audio_note_t *s_snd_notes;
static int s_snd_count;

static void snd_task(void *arg) {
  (void)arg;
  audio_i2s_play_song(s_snd_notes, s_snd_count, FX_AMP);
  s_snd_busy = false;
  vTaskDelete(NULL);
}

void game_fx(game_fx_t kind) {
  cue_t c = cue_for(kind);

  drv2605l_play_effect(c.effect);

  if (s_snd_busy)
    return;
  s_snd_busy = true;
  s_snd_notes = c.notes;
  s_snd_count = c.count;
  if (xTaskCreate(snd_task, "game_snd", SND_TASK_STACK, NULL, SND_TASK_PRIORITY, NULL) != pdPASS)
    s_snd_busy = false;
}
