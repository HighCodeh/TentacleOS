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

#include "test_platform.h"
#define VIEW_SAVED 1
#define VIEW_SAVED_INFO 2
#define VIEW_LIST 3
#define INPUT_BTN_DOWN 1
#define INPUT_BTN_UP 2
#define INPUT_BTN_OK 3
#define INPUT_BTN_BACK 4
#define UI_FB_NAV 1
#define UI_FB_WRITE 2
#define NOTIFY_INFO 1
#define NOTIFY_WARNING 2
static const char *TAG = "TEST";
static struct { const char *name; } s_saved[2] = {{"one"}, {"two"}};
static int s_saved_count, s_saved_sel, s_view;
static uintptr_t s_generation, s_delete_generation;
static unsigned builds, deletes;
static void (*pending)(void *);
static void *pending_context;
static void build_screen(void) { ++builds; }
static void update_sig_selection(void) {}
static void ui_feedback(int event) {}
static void notify(int level, const char *text) {}
static void led_signal_info(void) {}
static void led_signal_warning(void) {}
static esp_err_t subghz_storage_delete(const char *name) { ++deletes; return ESP_OK; }
static void ui_async_call(void (*fn)(void *), void *context) { pending = fn; pending_context = context; }
/* CALLBACKS */
static void input(int button) {
  struct { int button; } value = {button}, *ev = &value;
  bool press = true, nav = true;
  switch (s_view) {
/* SAVED_INPUT */
  }
}
int main(void) {
  s_view = VIEW_SAVED;
  input(INPUT_BTN_OK);
  assert(s_view == VIEW_SAVED && builds == 0);
  s_saved_count = 1;
  input(INPUT_BTN_OK);
  assert(s_view == VIEW_SAVED_INFO && builds == 1);
  s_delete_generation = s_generation;
  info_del_confirm(true);
  assert(deletes == 1 && pending);
  ui_subghz_menu_stop();
  pending(pending_context);
  assert(builds == 1);
  info_del_confirm(true);
  assert(deletes == 1);
  s_delete_generation = s_generation;
  info_del_confirm(true);
  pending(pending_context);
  assert(builds == 2 && deletes == 2);
  puts("PASS: empty saved list and stale delete/rebuild callbacks cannot navigate or delete");
}
