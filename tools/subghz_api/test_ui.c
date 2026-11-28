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


#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "subghz_replay.h"
#include "test_platform.h"
typedef int lv_timer_t;
typedef struct { int action, button; } input_event_t;
enum { INPUT_ACTION_PRESS, INPUT_ACTION_REPEAT, INPUT_ACTION_LONG_PRESS, INPUT_ACTION_RELEASE };
enum { INPUT_BTN_BACK, INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_LEFT, INPUT_BTN_RIGHT, INPUT_BTN_OK };
enum { SUBGHZ_SEND_LIST, SUBGHZ_SEND_DETAIL };
#define SCREEN_SUBGHZ_MENU 1
#define UI_FB_NAV 1
static const char *TAG = "TEST";
static int s_screen_storage, s_timer_storage, s_status_storage;
static void *s_screen = &s_screen_storage, *s_active, *s_status_label = &s_status_storage;
static lv_timer_t *s_refresh_timer = &s_timer_storage;
static unsigned s_stops, s_closed, s_sends, s_errors, s_details, s_backs;
static subghz_tx_status_t s_status;
static subghz_replay_t s_replay;
static bool s_owns_tx, s_is_pressed, s_is_repeating, s_is_active, s_is_direct;
static uint32_t s_sent_count;
static int s_view, s_sel, s_sig_count;
static struct {char name[40];} s_sigs[1];
static int s_send_error;
static bool s_button_down;
static bool input_is_down(int button) { assert(button == INPUT_BTN_OK); return s_button_down; }
static void *lv_screen_active(void) { return s_active; }
void subghz_tx_get_status(subghz_tx_status_t *out) { *out = s_status; }
void subghz_tx_stop(void) { ++s_stops; }
esp_err_t subghz_replay_send(const subghz_replay_t *replay) {
  assert(replay == &s_replay && replay->config.repeat_until_stopped); ++s_sends; return s_send_error;
}
void subghz_replay_free(subghz_replay_t *replay) { replay->pulses = NULL; }
static void ui_subghz_send_stop(void) { ++s_closed; }
static void lv_label_set_text(void *label, const char *text) {
  assert(label == s_status_label);
  if (strstr(text, "failed")) ++s_errors;
}
static void lv_label_set_text_fmt(void *label, const char *text, unsigned long count) {
  assert(label == s_status_label && count == s_sent_count);
}
void led_signal_error(void) {}
static void update_selection(void) {}
static void ui_feedback(int event) { assert(event == UI_FB_NAV); }
static void build_detail(const char *name) { ++s_details; s_view = SUBGHZ_SEND_DETAIL; }
static void build_list(void) {}
static void ui_switch_screen(int screen) { assert(screen == SCREEN_SUBGHZ_MENU); ++s_backs; }
/* CALLBACK */
static void event(int action, int button) {
  if (button == INPUT_BTN_OK) {
    if (action == INPUT_ACTION_PRESS) s_button_down = true;
    if (action == INPUT_ACTION_RELEASE) s_button_down = false;
  }
  input_event_t input = {action, button}; subghz_send_input(&input, NULL);
}
static void reset(void) {
  s_stops = s_closed = s_sends = s_errors = s_details = s_backs = 0;
  s_active = s_screen; s_view = SUBGHZ_SEND_DETAIL; s_send_error = ESP_OK;
  s_owns_tx = s_is_pressed = s_is_repeating = s_is_active = s_is_direct = false;
  s_sent_count = 0; s_status = (subghz_tx_status_t){0};
  s_replay.pulses = (int32_t *)&s_screen_storage;
  s_refresh_timer = &s_timer_storage;
}
int main(void) {
  reset(); s_view = SUBGHZ_SEND_LIST; s_sig_count = 0;
  event(INPUT_ACTION_PRESS, INPUT_BTN_OK); assert(!s_sends && !s_details);
  s_sig_count = 1; event(INPUT_ACTION_PRESS, INPUT_BTN_OK);
  event(INPUT_ACTION_LONG_PRESS, INPUT_BTN_OK);
  refresh_tick_cb(NULL); assert(s_details == 1 && !s_sends); // Opening never sends.
  event(INPUT_ACTION_RELEASE, INPUT_BTN_OK);
  event(INPUT_ACTION_PRESS, INPUT_BTN_OK); assert(s_sends == 1);
  event(INPUT_ACTION_RELEASE, INPUT_BTN_OK); assert(s_stops == 1 && !s_is_active);
  refresh_tick_cb(NULL); assert(s_sent_count == 0 && s_sends == 1);
  event(INPUT_ACTION_PRESS, INPUT_BTN_OK);
  s_status.is_busy = true;
  for (int i = 0; i < 20; ++i) { event(INPUT_ACTION_REPEAT, INPUT_BTN_OK); refresh_tick_cb(NULL); }
  assert(s_sends == 2); // No queued backlog while a burst is active.
  // Completion before LONG_PRESS must immediately queue the next burst.
  s_status.is_busy = false; refresh_tick_cb(NULL); assert(s_sends == 3 && s_sent_count == 1);
  event(INPUT_ACTION_LONG_PRESS, INPUT_BTN_OK); assert(s_sends == 3);
  event(INPUT_ACTION_RELEASE, INPUT_BTN_OK); assert(s_stops == 2 && !s_is_active);
  refresh_tick_cb(NULL); assert(s_sends == 3);
  reset(); event(INPUT_ACTION_PRESS, INPUT_BTN_OK); s_status.error = ESP_FAIL;
  refresh_tick_cb(NULL); assert(s_errors == 1 && s_stops == 1 && !s_sent_count);
  reset(); s_send_error = ESP_FAIL; event(INPUT_ACTION_PRESS, INPUT_BTN_OK);
  assert(s_errors == 1 && !s_is_active && !s_is_pressed);
  reset(); s_is_direct = true; event(INPUT_ACTION_PRESS, INPUT_BTN_OK);
  event(INPUT_ACTION_PRESS, INPUT_BTN_BACK); assert(s_stops == 1 && s_backs == 1);
  reset(); s_refresh_timer = NULL; event(INPUT_ACTION_PRESS, INPUT_BTN_OK); assert(!s_sends);
  reset(); event(INPUT_ACTION_PRESS, INPUT_BTN_OK);
  s_button_down = false; refresh_tick_cb(NULL); // Release event lost while a modal was open.
  assert(s_stops == 1 && !s_is_repeating && !s_is_active);
  reset(); s_active = NULL; refresh_tick_cb(NULL); assert(s_closed == 1);
  puts("PASS: immediate PTT repeat without LONG_PRESS, tap/release, opening edge, no backlog, errors, missing timer and Back");
}
