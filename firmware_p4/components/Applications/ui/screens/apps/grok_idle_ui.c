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

// Local, native-drawn Grok-inspired recreation, not an exported xAI animation.
// Deliberately no network client, microphone, speaker, account or SPI calls.
#include "grok_idle_ui.h"
#include <stdint.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "power_policy.h"

static lv_obj_t *s_screen, *s_avatar;
static lv_timer_t *s_timer;
static uint32_t s_generation, s_started, s_greet_started;
static int32_t s_diameter;
static bool s_running;

static int32_t wave(uint32_t elapsed, uint32_t period, int32_t amplitude) {
  return lv_trigo_sin((elapsed % period) * 360U / period) * amplitude / 32767;
}
static int32_t ease(int32_t value) {
  value = LV_CLAMP(0, value, 1000);
  return value * value / 1000 * (3000 - 2 * value) / 1000;
}
static int32_t blink(uint32_t phase, uint32_t start) {
  if (phase < start || phase >= start + 320U)
    return 0;
  uint32_t t = phase - start;
  if (t < 80U)
    return ease(t * 1000 / 80);
  if (t < 160U)
    return 1000;
  return 1000 - ease((t - 160U) * 1000 / 160);
}
static int32_t gaze(uint32_t elapsed) {
  static const uint16_t times[] = {0, 900, 1200, 2100, 2450, 4000, 4250, 5000, 5350, 8200};
  static const int8_t positions[] = {0, 0, -12, -12, -3, -3, 4, 4, 0, 0};
  uint32_t t = elapsed % 8200U;
  for (unsigned i = 1; i < sizeof(times) / sizeof(times[0]); ++i) {
    if (t < times[i])
      return positions[i - 1] + (positions[i] - positions[i - 1]) *
                                    ease((t - times[i - 1]) * 1000 / (times[i] - times[i - 1])) /
                                    1000;
  }
  return 0;
}
static void draw(lv_event_t *event) {
  if ((uint32_t)(uintptr_t)lv_event_get_user_data(event) != s_generation ||
      lv_event_get_target(event) != s_avatar)
    return;
  lv_layer_t *layer = lv_event_get_layer(event);
  lv_area_t area;
  lv_obj_get_coords(s_avatar, &area);
  uint32_t t = lv_tick_elaps(s_started), greeting = lv_tick_elaps(s_greet_started);
  int32_t nod = greeting < 1000U ? wave(greeting, 500, 5) * (1000 - (int32_t)greeting) / 1000 : 0;
  int32_t cx = (area.x1 + area.x2) / 2, cy = (area.y1 + area.y2) / 2 + wave(t, 3400, 3) + nod;
  int32_t squash = wave(t, 3400, 1);
  int32_t left = cx - s_diameter / 2 - squash, top = cy - s_diameter / 2 + squash;
  lv_draw_rect_dsc_t body;
  lv_draw_rect_dsc_init(&body);
  body.radius = LV_RADIUS_CIRCLE;
  body.bg_opa = LV_OPA_COVER;
  body.border_opa = LV_OPA_TRANSP;
  body.bg_grad.dir = LV_GRAD_DIR_VER;
  body.bg_grad.stops_count = 2;
  body.bg_grad.stops[0].frac = 0;
  body.bg_grad.stops[1].frac = 255;
  body.bg_grad.stops[0].opa = body.bg_grad.stops[1].opa = LV_OPA_COVER;
  body.bg_color = body.bg_grad.stops[0].color = lv_color_hex(0xFAFAFC);
  for (int32_t i = 0; i < 24; ++i) {
    uint8_t shade = 96 + i * 138 / 23;
    body.bg_grad.stops[1].color = lv_color_make(shade, shade, shade + 3);
    lv_area_t ring = {left + i / 5,
                      top + i / 6,
                      left + s_diameter + squash * 2 - i - 1,
                      top + s_diameter - squash * 2 - i - 1};
    lv_draw_rect(layer, &body, &ring);
  }
  int32_t close = LV_MAX(blink(t % 5600, 1250), blink(t % 5600, 3650));
  if ((t / 5600) % 2 == 0)
    close = LV_MAX(close, blink(t % 5600, 4070));
  for (int i = 0; i < 2; ++i) {
    int32_t x = cx + s_diameter * (i ? 31 : 10) / 100 + gaze(t);
    int32_t y = cy - s_diameter * (i ? 25 : 21) / 100 + nod / 2;
    lv_draw_line_dsc_t eye;
    lv_draw_line_dsc_init(&eye);
    eye.color = lv_color_hex(0x080809);
    eye.opa = LV_OPA_COVER;
    int32_t width = s_diameter * 7 / 80;
    eye.width = width + (3 - width) * close / 1000;
    eye.round_start = eye.round_end = true;
    int32_t dx = s_diameter / 32 + (s_diameter / 20 - s_diameter / 32) * close / 1000;
    int32_t dy = s_diameter / 16 * (1000 - close) / 1000;
    eye.p1.x = x - dx;
    eye.p1.y = y - dy;
    eye.p2.x = x + dx;
    eye.p2.y = y + dy;
    lv_draw_line(layer, &eye);
  }
}
static void tick(lv_timer_t *timer) {
  if (s_running && timer == s_timer && s_avatar &&
      (uint32_t)(uintptr_t)lv_timer_get_user_data(timer) == s_generation)
    lv_obj_invalidate(s_avatar);
}
void ui_grok_idle_stop(void) {
  s_running = false;
  if (s_timer) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (s_screen)
    power_policy_set_display_hold(s_screen, false);
}
static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen ||
      (uint32_t)(uintptr_t)lv_event_get_user_data(event) != s_generation)
    return;
  ui_grok_idle_stop();
  s_screen = s_avatar = NULL;
}
static void input(const input_event_t *event, void *context) {
  if (!s_running || (uint32_t)(uintptr_t)context != s_generation ||
      event->action != INPUT_ACTION_PRESS)
    return;
  if (event->button == INPUT_BTN_BACK)
    ui_switch_screen(SCREEN_GAMES_MENU);
  else if (event->button == INPUT_BTN_OK)
    s_greet_started = lv_tick_get();
}
void ui_grok_idle_open(void) {
  lv_obj_t *next = lv_obj_create(NULL);
  if (!next)
    return;
  ui_grok_idle_stop();
  lv_obj_t *previous = s_screen;
  ++s_generation;
  s_screen = next;
  s_avatar = NULL;
  s_started = lv_tick_get();
  s_greet_started = s_started;
  lv_obj_remove_style_all(s_screen);
  lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, (void *)(uintptr_t)s_generation);
  int32_t w = lv_display_get_horizontal_resolution(NULL),
          h = lv_display_get_vertical_resolution(NULL);
  s_diameter = LV_MAX(48, LV_MIN(190, LV_MIN(w, h) - 48));
  s_avatar = lv_obj_create(s_screen);
  if (s_avatar) {
    lv_obj_remove_style_all(s_avatar);
    lv_obj_remove_flag(s_avatar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_avatar, s_diameter + 32, s_diameter + 32);
    lv_obj_center(s_avatar);
    lv_obj_add_event_cb(s_avatar, draw, LV_EVENT_DRAW_MAIN, (void *)(uintptr_t)s_generation);
  }
  s_running = true;
  power_policy_set_display_hold(s_screen, true);
  ui_input_set_screen_handler(input, (void *)(uintptr_t)s_generation);
  ui_screen_load(s_screen);
  if (previous && previous != s_screen && lv_obj_is_valid(previous))
    lv_obj_delete(previous);
  // Allocation failure leaves a static, navigable image. No capture can start.
  if (s_avatar)
    s_timer = lv_timer_create(tick, 50, (void *)(uintptr_t)s_generation);
}
