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

#include "ui_feedback.h"

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_i2s.h"
#include "drv2605l.h"

#define FB_TASK_STACK_SIZE 4096
#define FB_TASK_PRIORITY   4

#define DRV_EFFECT_STRONG_CLICK 1
#define DRV_EFFECT_SHARP_CLICK  4
#define DRV_EFFECT_DOUBLE_CLICK 10

typedef struct {
  const audio_note_t *notes;
  int count;
  float amp;
  uint8_t haptic;
} fb_def_t;

static const audio_note_t SND_NAV[] = {{2000, 32}};
static const audio_note_t SND_SELECT[] = {{1568, 40}};
static const audio_note_t SND_READ[] = {{1318, 60}, {1976, 95}};
static const audio_note_t SND_WRITE[] = {{1568, 45}, {2093, 80}};
static const audio_note_t SND_EMULATE[] = {{1046, 70}, {1568, 95}};
static const audio_note_t SND_BOOT[] = {{523, 120}, {659, 120}, {784, 175}};

static const fb_def_t DEFS[UI_FB_COUNT] = {
    [UI_FB_NAV] = {SND_NAV, 1, 0.30f, 0},
    [UI_FB_SELECT] = {SND_SELECT, 1, 0.32f, 0},
    [UI_FB_READ] = {SND_READ, 2, 0.40f, DRV_EFFECT_STRONG_CLICK},
    [UI_FB_WRITE] = {SND_WRITE, 2, 0.40f, DRV_EFFECT_DOUBLE_CLICK},
    [UI_FB_EMULATE] = {SND_EMULATE, 2, 0.40f, DRV_EFFECT_SHARP_CLICK},
    [UI_FB_BOOT] = {SND_BOOT, 3, 0.35f, 0},
};

static volatile bool s_busy = false;

static void fb_task(void *arg);

void ui_feedback_init(void) {}

void ui_feedback(ui_feedback_kind_t kind) {
  if ((int)kind < 0 || kind >= UI_FB_COUNT)
    return;
  if (s_busy)
    return;
  s_busy = true;
  if (xTaskCreate(
          fb_task, "ui_fb", FB_TASK_STACK_SIZE, (void *)(intptr_t)kind, FB_TASK_PRIORITY, NULL) !=
      pdPASS)
    s_busy = false;
}

static void fb_task(void *arg) {
  ui_feedback_kind_t k = (ui_feedback_kind_t)(intptr_t)arg;
  const fb_def_t *d = &DEFS[k];
  if (d->haptic)
    (void)drv2605l_play_effect(d->haptic);
  if (d->notes && d->count > 0)
    (void)audio_i2s_play_song(d->notes, d->count, d->amp);
  s_busy = false;
  vTaskDelete(NULL);
}
