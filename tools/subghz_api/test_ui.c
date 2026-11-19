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

typedef int lv_timer_t;
typedef struct { bool is_busy; int error; } subghz_tx_status_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define NOTIFY_WARNING 1
#define UI_FB_WRITE 2
#define VIEW_LIST 3
#define VIEW_SENT 4
#define STATUS_SENT "Signal sent"
#define HINT_SENT "Sent"
#define SEND_DONE_MS 100
static int s_screen_storage, s_timer_storage, s_status_storage;
static void *s_screen = &s_screen_storage;
static void *s_active;
static unsigned s_stops, s_closed, s_success, s_failures, s_locked, s_lists;
static subghz_tx_status_t s_status;
static void *lv_screen_active(void) { return s_active; }
static void subghz_tx_get_status(subghz_tx_status_t *out) { *out = s_status; }
static void stop_tx(void) { ++s_stops; }
static void subghz_scope_lock(void) { ++s_locked; }
static void ui_feedback(int event) { assert(event == UI_FB_WRITE); ++s_success; }
#ifdef TEST_SEND_SCREEN
static int s_view;
static bool s_options;
static uint32_t s_sent_at;
static lv_timer_t *s_send_timer;
static void ui_subghz_send_stop(void) { ++s_closed; }
static void stop_send_timer(void) { s_send_timer = NULL; }
static void build_list(void) { ++s_lists; }
static void notify(int kind, const char *message) {
  assert(kind == NOTIFY_WARNING && strcmp(message, "Transmission failed") == 0);
  ++s_failures;
}
static void set_status(const char *text, bool success) {
  assert(success && strcmp(text, STATUS_SENT) == 0);
}
static void set_hint(const char *text) { assert(strcmp(text, HINT_SENT) == 0); }
static uint32_t lv_tick_get(void) { return 42; }
#define POLL send_done_cb
#else
static lv_timer_t *s_send_t1, *s_send_t2;
static void *s_send_status = &s_status_storage;
static struct { int text_main; } current_theme;
static bool s_fail_timer;
static void ui_subghz_menu_stop(void) { ++s_closed; }
static void lv_timer_delete(lv_timer_t *timer) { assert(timer == &s_timer_storage); }
static void subghz_scope_stop(void) {}
static void lv_label_set_text(void *label, const char *text) {
  assert(label == s_send_status);
  if (strcmp(text, "Transmission failed. BACK to exit") == 0) ++s_failures;
  else assert(strcmp(text, "Signal sent!") == 0);
}
static void lv_obj_set_style_text_color(void *label, int color, int part) {}
static void send_finish_cb(lv_timer_t *timer) {}
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), unsigned ms, void *arg) {
  assert(cb == send_finish_cb && ms == SEND_DONE_MS);
  return s_fail_timer ? NULL : &s_timer_storage;
}
static void lv_timer_set_repeat_count(lv_timer_t *timer, unsigned count) {
  assert(timer != NULL && count == 1);
}
#define POLL send_lock_cb
#endif

/* CALLBACK */

static void reset(void) {
  s_stops = s_closed = s_success = s_failures = s_locked = s_lists = 0;
  s_active = s_screen;
#ifdef TEST_SEND_SCREEN
  s_send_timer = &s_timer_storage; s_view = 0;
#else
  s_send_t1 = &s_timer_storage; s_send_t2 = NULL;
#endif
}
int main(void) {
  reset(); s_status = (subghz_tx_status_t){.is_busy = true, .error = ESP_OK};
  POLL(&s_timer_storage);
  assert(s_stops == 0 && s_success == 0 && s_failures == 0 && s_locked == 0);
  reset(); s_status.error = ESP_FAIL;
  POLL(&s_timer_storage);
  assert(s_stops == 1 && s_failures == 1 && s_success == 0 && s_locked == 0);
#ifdef TEST_SEND_SCREEN
  assert(s_view == VIEW_LIST && s_lists == 1 && s_send_timer == NULL);
#else
  assert(s_send_t1 == NULL && s_send_t2 == NULL);
#endif
  reset(); s_status = (subghz_tx_status_t){.error = ESP_OK};
  POLL(&s_timer_storage);
  assert(s_stops == 1 && s_success == 1 && s_locked == 1 && s_failures == 0);
#ifdef TEST_SEND_SCREEN
  assert(s_view == VIEW_SENT && s_sent_at == 42);
#else
  assert(s_send_t1 == NULL && s_send_t2 != NULL);
  reset(); s_fail_timer = true; POLL(&s_timer_storage);
  assert(s_send_t2 == NULL && s_success == 1);
#endif
  reset(); s_active = NULL;
  POLL(&s_timer_storage);
  assert(s_closed == 1 && s_success == 0);
  puts("PASS: production Sub-GHz UI callback waits for completion, shows failures and stops on exit");
}
