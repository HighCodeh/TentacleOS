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

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "audio_i2s.h"
#include "drv2605l.h"

#define FB_TASK_STACK_SIZE 3072
#define FB_TASK_PRIORITY SYS_PRIO_SERVICE_LO

/** @brief Single pending slot. Feedback is a reaction to an input that already
 *         happened - a stale one is worthless, so we drop instead of queueing.
 *         Spinning the coverflow must never build up a backlog of chirps. */
#define FB_QUEUE_DEPTH 1

/** @brief Minimum spacing between two accepted bursts.
 *
 * The s_playing flag alone is not enough: the worker clears it the instant a
 * burst ends, and there is a window between it taking an item off the queue and
 * setting the flag. A keypad repeat firing several LV_EVENT_KEY in quick
 * succession slips through those gaps and the chirps pile onto each other.
 * This is a hard floor on the rate, independent of where the events come from.
 * Slightly longer than the ~48 ms UI_FB_NAV burst so held navigation ticks
 * cleanly instead of running the sounds together. */
#define FB_MIN_GAP_MS 120

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
static const audio_note_t SND_SD_IN[] = {{1046, 45}, {1568, 55}, {2093, 80}};
static const audio_note_t SND_SD_OUT[] = {{2093, 45}, {1568, 55}, {1046, 80}};

static const fb_def_t DEFS[UI_FB_COUNT] = {
    [UI_FB_NAV] = {SND_NAV, 1, 0.30f, 0},
    [UI_FB_SELECT] = {SND_SELECT, 1, 0.32f, 0},
    [UI_FB_READ] = {SND_READ, 2, 0.40f, DRV_EFFECT_STRONG_CLICK},
    [UI_FB_WRITE] = {SND_WRITE, 2, 0.40f, DRV_EFFECT_DOUBLE_CLICK},
    [UI_FB_EMULATE] = {SND_EMULATE, 2, 0.40f, DRV_EFFECT_SHARP_CLICK},
    [UI_FB_BOOT] = {SND_BOOT, 3, 0.35f, 0},
    [UI_FB_SD_CONNECT] = {SND_SD_IN, 3, 0.40f, DRV_EFFECT_STRONG_CLICK},
    [UI_FB_SD_DISCONNECT] = {SND_SD_OUT, 3, 0.35f, 0},
};

/** @brief True from the moment a burst is ACCEPTED until the worker has fully
 *         finished playing it. Set by the producer (not the worker) so there is
 *         no window between accepting and marking - that gap is exactly how two
 *         requests used to slip through and run into each other. */
static bool s_playing = false;

/** @brief Monotonic ms of the last accepted burst, for the FB_MIN_GAP_MS floor. */
static int64_t s_last_accept_ms = 0;

/** @brief Guards s_playing + s_last_accept_ms. ui_feedback() has more than one
 *         producer: UI navigation runs on the LVGL thread while SD insert /
 *         removal comes from the storage monitor task, so the accept decision
 *         has to be atomic against itself. */
static portMUX_TYPE s_fb_lock = portMUX_INITIALIZER_UNLOCKED;

static QueueHandle_t s_fb_q = NULL;

static void fb_task(void *arg);

void ui_feedback_init(void) {
  if (s_fb_q != NULL) // ui_manager calls this on every UI (re)start
    return;

  s_fb_q = xQueueCreate(FB_QUEUE_DEPTH, sizeof(ui_feedback_kind_t));
  if (s_fb_q == NULL)
    return;

  // One long-lived worker instead of a task per event: the old code spawned a
  // 6 KB task on every nav tick, which under a fast coverflow spin meant many
  // live tasks all racing for the single I2S TX channel.
  //
  // Pinned to SYS_CORE_RADIO, not the UI core: this worker renders the tone and
  // feeds the I2S DMA, and the LVGL renderer sits above it in priority on the
  // UI core - a full-frame redraw would preempt it long enough to starve the
  // sample feed. Same reason the driver's own FX task lives there.
  if (xTaskCreatePinnedToCore(
          fb_task, "ui_fb", FB_TASK_STACK_SIZE, NULL, FB_TASK_PRIORITY, NULL, SYS_CORE_RADIO) !=
      pdPASS) {
    vQueueDelete(s_fb_q);
    s_fb_q = NULL;
  }
}

void ui_feedback(ui_feedback_kind_t kind) {
  if ((int)kind < 0 || kind >= UI_FB_COUNT)
    return;
  if (s_fb_q == NULL)
    return;

  const int64_t now_ms = esp_timer_get_time() / 1000; // outside the critical section

  // Claim the slot atomically: one burst in flight at a time, and never two
  // closer together than FB_MIN_GAP_MS, no matter which task is asking.
  bool accepted = false;
  portENTER_CRITICAL(&s_fb_lock);
  if (!s_playing && (now_ms - s_last_accept_ms) >= FB_MIN_GAP_MS) {
    s_playing = true;
    s_last_accept_ms = now_ms;
    accepted = true;
  }
  portEXIT_CRITICAL(&s_fb_lock);

  if (!accepted)
    return;

  // Depth-1 queue with a zero timeout: never blocks a caller, and the slot is
  // guaranteed free because we just claimed s_playing. Release the claim if the
  // send somehow fails, otherwise feedback would be wedged off forever.
  if (xQueueSend(s_fb_q, &kind, 0) != pdTRUE) {
    portENTER_CRITICAL(&s_fb_lock);
    s_playing = false;
    portEXIT_CRITICAL(&s_fb_lock);
  }
}

static void fb_task(void *arg) {
  (void)arg;
  ui_feedback_kind_t k;
  while (true) {
    if (xQueueReceive(s_fb_q, &k, portMAX_DELAY) != pdTRUE)
      continue;
    if ((int)k < 0 || k >= UI_FB_COUNT) {
      portENTER_CRITICAL(&s_fb_lock);
      s_playing = false;
      portEXIT_CRITICAL(&s_fb_lock);
      continue;
    }

    // s_playing was already set by the producer that claimed this slot.
    const fb_def_t *d = &DEFS[k];
    if (d->haptic)
      (void)drv2605l_play_effect(d->haptic);
    if (d->notes && d->count > 0)
      (void)audio_i2s_play_song(d->notes, d->count, d->amp);

    // play_song only returns once the samples have actually reached the amp
    // (the driver drains the DMA before releasing), so releasing the claim here
    // means the next burst starts on silence instead of on top of this one.
    portENTER_CRITICAL(&s_fb_lock);
    s_playing = false;
    portEXIT_CRITICAL(&s_fb_lock);
  }
}
