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

/* Native LVGL interface, official upstream renderer (not recreated artwork).
 * Every connected/listening/reply state comes from the actual service. Idle
 * blinking is only a local animation. Checked SDK currently returns TEXT,
 * not spoken audio; SPEAKING is deliberately never displayed or simulated. */
#include "muse_ui.h"
#include "hb_muse_p4_service.h"
#include "hb_muse_app.h"
#include "muse_pixel.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "power_policy.h"
#include "ui_manager.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_screen, *s_avatar, *s_status, *s_detail, *s_footer, *s_dialog, *s_choices;
static lv_obj_t *s_reply_box, *s_reply;
static lv_timer_t *s_timer;
static uint16_t *s_pixels;
static lv_image_dsc_t s_image;
static uint32_t s_generation, s_mode_since;
static bool s_running, s_allow, s_reply_shown;
static hb_muse_p4_operation_t s_confirm_operation, s_operation;
static muse_mode_t s_mode;
static hb_muse_p4_view_t s_view;
static char s_reply_copy[1024];

static void place_avatar(bool reply) {
  if (!s_avatar)
    return;
  int size = lv_display_get_vertical_resolution(NULL) - 112;
  int width = lv_display_get_horizontal_resolution(NULL) - 16;
  if (size > width)
    size = width;
  if (size > 200)
    size = 200;
  if (size < 64)
    size = 64;
  lv_image_set_scale(s_avatar, reply ? 256 : (uint32_t)size * 4);
  lv_obj_align(s_avatar, reply ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, reply ? 34 : -38);
}

static lv_obj_t *label(lv_obj_t *parent, int y, const char *text, uint32_t color) {
  lv_obj_t *obj = lv_label_create(parent);
  if (!obj)
    return NULL;
  lv_obj_set_width(obj, lv_display_get_horizontal_resolution(NULL) - 20);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
  return obj;
}
static void render(void) {
  if (!s_pixels || !s_avatar)
    return;
  muse_mode_t mode = s_view.error ? MUSE_MODE_ERROR
                     : s_view.mic ? MUSE_MODE_LISTENING
                     : (s_view.phase == HB_MUSE_APP_WAITING || s_view.phase == HB_MUSE_APP_SENDING)
                         ? MUSE_MODE_THINKING
                         : MUSE_MODE_IDLE;
  if (mode != s_mode) {
    s_mode = mode;
    s_mode_since = lv_tick_get();
  }
  muse_pose_t pose = {.mode = mode,
                      .t = (float)lv_tick_get() / 1000,
                      .mode_t = (float)lv_tick_elaps(s_mode_since) / 1000,
                      .level = s_view.mic ? (float)s_view.level / 100 : 0,
                      .happy = 0};
  muse_pixel_render(&pose);
  muse_pixel_set_size(64);
  muse_pixel_scale(s_pixels, 64, 0, 63, 0, 63);
  lv_image_cache_drop(&s_image);
  lv_obj_invalidate(s_avatar);
}
static void tick(lv_timer_t *timer) {
  if (!s_running || timer != s_timer ||
      (uint32_t)(uintptr_t)lv_timer_get_user_data(timer) != s_generation)
    return;
  hb_muse_p4_view(&s_view);
  if (s_status)
    lv_label_set_text(s_status, s_view.status[0] ? s_view.status : "Offline");
  if (s_detail)
    lv_label_set_text(s_detail,
                      s_view.detail[0] ? s_view.detail : "Local animation / microphone off");
  bool reply = s_view.reply[0] != 0;
  if (reply != s_reply_shown) {
    s_reply_shown = reply;
    if (s_reply_box) {
      if (reply)
        lv_obj_remove_flag(s_reply_box, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(s_reply_box, LV_OBJ_FLAG_HIDDEN);
    }
    place_avatar(reply);
  }
  if (s_reply && strcmp(s_reply_copy, s_view.reply)) {
    memcpy(s_reply_copy, s_view.reply, sizeof s_reply_copy);
    s_reply_copy[sizeof s_reply_copy - 1] = 0;
    lv_label_set_text(s_reply, s_reply_copy);
    lv_obj_scroll_to_y(s_reply_box, 0, LV_ANIM_OFF);
  }
  if (s_footer)
    lv_label_set_text_static(
        s_footer,
        s_view.confirming ? "OK: press, release, press to approve"
        : s_view.active   ? (s_operation != HB_MUSE_P4_CHAT ? "Setup only / mic off / BACK: cancel"
                             : s_view.phase == HB_MUSE_APP_WAITING ? "Waiting / microphone off"
                             : s_view.phase == HB_MUSE_APP_SENDING ? "Sending / microphone off"
                             : s_view.ready                        ? "Hold OK to talk / BACK to stop"
                                                                   : "Connecting / microphone off")
                          : "RIGHT: connect / LEFT: setup");
  render();
}
void ui_muse_stop(void) {
  s_running = false;
  hb_muse_p4_stop();
  if (s_screen)
    power_policy_set_display_hold(s_screen, false);
  if (s_timer) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
}
static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen ||
      (uint32_t)(uintptr_t)lv_event_get_user_data(event) != s_generation)
    return;
  ui_muse_stop();
  if (s_pixels) {
    lv_image_cache_drop(&s_image);
    heap_caps_free(s_pixels);
    s_pixels = NULL;
  }
  s_screen = s_avatar = s_status = s_detail = s_footer = s_dialog = s_choices = s_reply_box =
      s_reply = NULL;
}
static void confirm(hb_muse_p4_operation_t operation) {
  if (s_dialog || hb_muse_p4_is_active() || !s_timer)
    return;
  s_confirm_operation = operation;
  s_allow = false;
  s_dialog = lv_obj_create(s_screen);
  lv_obj_remove_style_all(s_dialog);
  lv_obj_set_size(s_dialog, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_dialog, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_dialog, LV_OPA_COVER, 0);
  label(s_dialog,
        18,
        operation == HB_MUSE_P4_CHAT    ? "Connect to Muse?"
        : operation == HB_MUSE_P4_SETUP ? "Pair with Muse app?"
                                        : "Load SDK token from Mac?",
        0xF2F2F5);
  label(
      s_dialog,
      55,
      operation == HB_MUSE_P4_CHAT
          ? "Uses your paired Muse account.\nYour voice note goes to the service.\nHold OK to "
            "record; release to send.\nText replies, no speaker output.\nSession limit: 2 minutes."
      : operation == HB_MUSE_P4_SETUP
          ? "Enables nearby Bluetooth pairing\nfor at most 2 minutes.\nConfirm the real phone "
            "request\non this device. No microphone\nor cloud chat is started."
          : "Opens a local C5 USB input window.\nUse the reviewed Mac loader.\nThe token stays on "
            "C5, never P4.\nDo not paste it into chat.\nNo microphone or cloud chat.",
      0xB6AFCA);
  int h = lv_display_get_vertical_resolution(NULL);
  s_choices = label(s_dialog, h - 65, "> Cancel\n  Continue", 0xCFB6FF);
  label(s_dialog, h - 25, "UP/DOWN: choose / OK: confirm", 0x9991AA);
}
static void input(const input_event_t *event, void *ctx) {
  if (!s_running || (uint32_t)(uintptr_t)ctx != s_generation)
    return;
  bool press = event->action == INPUT_ACTION_PRESS;
  if (event->button == INPUT_BTN_OK && event->action == INPUT_ACTION_RELEASE)
    hb_muse_p4_key(false);
  if (s_dialog) {
    if (press && (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN)) {
      s_allow = !s_allow;
      lv_label_set_text_static(s_choices,
                               s_allow ? "  Cancel\n> Continue" : "> Cancel\n  Continue");
    }
    if (press && (event->button == INPUT_BTN_OK || event->button == INPUT_BTN_BACK)) {
      bool yes = event->button == INPUT_BTN_OK && s_allow;
      lv_obj_delete(s_dialog);
      s_dialog = s_choices = NULL;
      if (yes) {
        s_operation = s_confirm_operation;
        if (hb_muse_p4_start(s_confirm_operation) != ESP_OK)
          lv_label_set_text_static(s_detail, "Could not start / previous task may be stopping");
      }
    }
    return;
  }
  if (press && event->button == INPUT_BTN_BACK) {
    hb_muse_p4_stop();
    ui_switch_screen(SCREEN_GAMES_MENU);
    return;
  }
  if (event->button == INPUT_BTN_OK && press) {
    hb_muse_p4_key(true);
    return;
  }
  if (s_reply_shown && press &&
      (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN)) {
    lv_obj_scroll_by(s_reply_box, 0, event->button == INPUT_BTN_UP ? 34 : -34, LV_ANIM_OFF);
    return;
  }
  if (!hb_muse_p4_is_active() && press) {
    if (event->button == INPUT_BTN_LEFT)
      confirm(HB_MUSE_P4_SETUP);
    if (event->button == INPUT_BTN_RIGHT)
      confirm(HB_MUSE_P4_CHAT);
    if (event->button == INPUT_BTN_DOWN)
      confirm(HB_MUSE_P4_TOKEN);
  }
}
void ui_muse_open(void) {
  lv_obj_t *next = lv_obj_create(NULL);
  if (!next)
    return;
  ui_muse_stop();
  lv_obj_t *old = s_screen;
  if (old && lv_obj_is_valid(old)) { /* detach old image before releasing its owned buffer */
    if (s_avatar)
      lv_image_set_src(s_avatar, NULL);
    if (s_pixels) {
      lv_image_cache_drop(&s_image);
      heap_caps_free(s_pixels);
      s_pixels = NULL;
    }
  }
  ++s_generation;
  s_screen = next;
  s_dialog = s_choices = NULL;
  s_reply_copy[0] = 0;
  s_reply_shown = false;
  memset(&s_view, 0, sizeof s_view);
  s_mode = MUSE_MODE_IDLE;
  s_mode_since = lv_tick_get();
  lv_obj_remove_style_all(next);
  lv_obj_set_style_bg_color(next, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(next, LV_OPA_COVER, 0);
  lv_obj_remove_flag(next, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(next, deleted, LV_EVENT_DELETE, (void *)(uintptr_t)s_generation);
  int h = lv_display_get_vertical_resolution(NULL), w = lv_display_get_horizontal_resolution(NULL);
  s_pixels = heap_caps_calloc(64 * 64, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_avatar = NULL;
  if (s_pixels) {
    memset(&s_image, 0, sizeof s_image);
    s_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_image.header.cf = LV_COLOR_FORMAT_RGB565;
    s_image.header.w = 64;
    s_image.header.h = 64;
    s_image.header.stride = 128;
    s_image.data_size = 8192;
    s_image.data = (uint8_t *)s_pixels;
    s_avatar = lv_image_create(next);
    lv_image_set_src(s_avatar, &s_image);
    lv_image_set_antialias(s_avatar, false);
    place_avatar(false);
  }
  s_reply_box = lv_obj_create(next);
  lv_obj_set_size(s_reply_box, w - 16, h - 190);
  lv_obj_align(s_reply_box, LV_ALIGN_TOP_MID, 0, 105);
  lv_obj_set_style_bg_color(s_reply_box, lv_color_hex(0x171020), 0);
  lv_obj_set_style_bg_opa(s_reply_box, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_reply_box, 10, 0);
  lv_obj_set_style_border_width(s_reply_box, 0, 0);
  lv_obj_set_style_pad_all(s_reply_box, 8, 0);
  s_reply = lv_label_create(s_reply_box);
  lv_obj_set_width(s_reply, LV_PCT(100));
  lv_label_set_long_mode(s_reply, LV_LABEL_LONG_WRAP);
  lv_label_set_text_static(s_reply, "");
  lv_obj_set_style_text_color(s_reply, lv_color_hex(0xE5DFEF), 0);
  lv_obj_set_style_text_font(s_reply, &lv_font_montserrat_12, 0);
  lv_obj_add_flag(s_reply_box, LV_OBJ_FLAG_HIDDEN);
  s_status = label(next, h - 88, "Offline", 0xCFB6FF);
  s_detail = label(next,
                   h - 68,
                   s_pixels ? "Local animation / microphone off" : "Avatar memory unavailable",
                   0xA8A0B6);
  s_footer = label(next, h - 33, "", 0x9991AA);
  s_running = true;
  power_policy_set_display_hold(next, true);
  ui_input_set_screen_handler(input, (void *)(uintptr_t)s_generation);
  ui_screen_load(next);
  if (old && old != next && lv_obj_is_valid(old))
    lv_obj_delete(old);
  if (s_pixels)
    s_timer = lv_timer_create(tick, 80, (void *)(uintptr_t)s_generation);
  render();
}
