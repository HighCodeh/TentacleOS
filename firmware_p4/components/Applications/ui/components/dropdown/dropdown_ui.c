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

#include "dropdown_ui.h"

#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st7789.h"
#include "sys_prio.h"

#include "assets_manager.h"
#include "audio_i2s.h"
#include "battery_service.h"
#include "bluetooth_service.h"
#include "buttons_gpio.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "notify_ui.h"
#include "reboot_ui.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "tutorial_ui.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "vfs_sdcard.h"
#include "wifi_service.h"

#define GREEN    0x00E676
#define CHIP_BG  current_theme.screen_base
#define PANEL_BG current_theme.bg_secondary
#define SL_STEP  10

#define SLIDE_ANIM_MS       300
#define SLIDE_BTN_POLL_MS   50
#define SLIDER_MIN_FILL_PCT 3
#define BAT_ANIM_MS         350
#define LONGPRESS_MS        450 // hold UP this long to toggle the dropdown

#define ROW_BADGES 0
#define ROW_BRIGHT 1
#define ROW_SOUND  2
#define ROW_COUNT  3
static int focus_row = ROW_BADGES;

#define BADGE_COUNT   4
#define BADGE_WIFI    0
#define BADGE_BLE     1
#define BADGE_SD      2
#define BADGE_REBOOT  3
#define ACTION_ACCENT 0xF5B13D
#define ARMED_ACCENT  0xFF5252
static lv_obj_t *badge_dot[BADGE_COUNT] = {NULL};
static lv_obj_t *badge_ic[BADGE_COUNT] = {NULL};
static lv_obj_t *badge_lbl[BADGE_COUNT] = {NULL};
static bool badge_on[BADGE_COUNT] = {false, false, false, false};
static const bool BADGE_ACTION[BADGE_COUNT] = {false, false, true, true};
static int badge_sel = 0;
static int s_sd_armed = 0;
static bool s_sd_present_last = false;

static lv_obj_t *sd_chip_val = NULL;
static lv_obj_t *sd_chip_fill = NULL;

static lv_obj_t *bat_chip_val = NULL;
static lv_obj_t *bat_chip_fill = NULL;
static int bat_anim_pct = 0;
static uint32_t bat_last_anim = 0;

#define SLIDER_COUNT 2
#define SLIDER_SOUND 1
static lv_obj_t *sl_track[SLIDER_COUNT] = {NULL};
static lv_obj_t *sl_fill[SLIDER_COUNT] = {NULL};
static lv_obj_t *sl_knob[SLIDER_COUNT] = {NULL};
static lv_obj_t *sl_icon[SLIDER_COUNT] = {NULL};
static lv_obj_t *sl_val[SLIDER_COUNT] = {NULL};
static int sl_value[SLIDER_COUNT] = {80, 45};
static bool s_vol_dirty = false;

static lv_obj_t *slide_panel = NULL;
static int s_panel_h = 0;
static bool slide_open = false;
static bool slide_animating = false;

static lv_obj_t **hide_objs_ref = NULL;
static int hide_objs_count = 0;

static bool btn_up_last, btn_down_last, btn_left_last, btn_right_last, btn_ok_last, btn_back_last;
static lv_timer_t *slide_btn_timer = NULL;

static uint32_t up_hold_start = 0;    // tick when UP was pressed (long-press timing)
static bool up_hold_consumed = false; // long-press already toggled for this hold

static void refresh_sd_status(void);
static void battery_tick(void);

static bool badge_is_enabled(int idx) {
  if (idx == BADGE_SD)
    return vfs_sdcard_is_mounted();
  return true;
}

static void badge_step(int dir) {
  int next = badge_sel;
  for (int i = 0; i < BADGE_COUNT; i++) {
    next = (next + dir + BADGE_COUNT) % BADGE_COUNT;
    if (badge_is_enabled(next))
      break;
  }
  badge_sel = next;
}

static void refresh_focus(void) {
  for (int i = 0; i < BADGE_COUNT; i++) {
    if (!badge_dot[i])
      continue;
    bool sel = (focus_row == ROW_BADGES && i == badge_sel);
    if (BADGE_ACTION[i]) {
      if (i == BADGE_SD && !badge_is_enabled(BADGE_SD)) {
        lv_color_t dim = current_theme.border_inactive;
        lv_obj_set_style_border_color(badge_dot[i], dim, 0);
        lv_obj_set_style_border_width(badge_dot[i], 2, 0);
        lv_obj_set_style_bg_color(badge_dot[i], CHIP_BG, 0);
        lv_obj_set_style_shadow_width(badge_dot[i], 0, 0);
        lv_obj_set_style_shadow_opa(badge_dot[i], LV_OPA_TRANSP, 0);
        if (badge_ic[i])
          lv_obj_set_style_text_color(badge_ic[i], dim, 0);
        if (badge_lbl[i])
          lv_obj_set_style_text_color(badge_lbl[i], dim, 0);
        continue;
      }
      lv_color_t acc =
          (i == BADGE_SD && s_sd_armed) ? lv_color_hex(ARMED_ACCENT) : lv_color_hex(ACTION_ACCENT);
      lv_obj_set_style_border_color(badge_dot[i], sel ? current_theme.border_accent : acc, 0);
      lv_obj_set_style_border_width(badge_dot[i], sel ? 3 : 2, 0);
      lv_obj_set_style_bg_color(badge_dot[i], CHIP_BG, 0);
      lv_obj_set_style_shadow_width(badge_dot[i], sel ? 10 : 0, 0);
      lv_obj_set_style_shadow_color(badge_dot[i], acc, 0);
      lv_obj_set_style_shadow_opa(badge_dot[i], sel ? LV_OPA_40 : LV_OPA_TRANSP, 0);
      if (badge_ic[i])
        lv_obj_set_style_text_color(badge_ic[i], acc, 0);
      if (badge_lbl[i])
        lv_obj_set_style_text_color(badge_lbl[i], sel ? current_theme.border_accent : acc, 0);
      continue;
    }
    bool on = badge_on[i];
    lv_color_t conn = on ? lv_color_hex(GREEN) : current_theme.border_inactive;
    lv_obj_set_style_border_color(badge_dot[i], sel ? current_theme.border_accent : conn, 0);
    lv_obj_set_style_border_width(badge_dot[i], sel ? 3 : 2, 0);
    lv_obj_set_style_bg_color(badge_dot[i], on ? lv_color_hex(0x04160C) : CHIP_BG, 0);
    lv_obj_set_style_shadow_width(badge_dot[i], on ? 12 : 0, 0);
    lv_obj_set_style_shadow_color(badge_dot[i], lv_color_hex(GREEN), 0);
    lv_obj_set_style_shadow_opa(badge_dot[i], on ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    if (badge_ic[i])
      lv_obj_set_style_text_color(
          badge_ic[i], on ? lv_color_hex(GREEN) : current_theme.border_inactive, 0);
    if (badge_lbl[i])
      lv_obj_set_style_text_color(badge_lbl[i], sel ? current_theme.border_accent : conn, 0);
  }
  for (int s = 0; s < SLIDER_COUNT; s++) {
    if (!sl_track[s])
      continue;
    bool foc = (focus_row == ROW_BRIGHT + s);
    lv_obj_set_style_border_width(sl_track[s], foc ? 2 : 1, 0);
    lv_obj_set_style_border_color(
        sl_track[s], foc ? current_theme.border_accent : current_theme.border_inactive, 0);
    if (sl_icon[s])
      lv_obj_set_style_image_recolor_opa(sl_icon[s], foc ? LV_OPA_TRANSP : LV_OPA_50, 0);
    if (sl_val[s])
      lv_obj_set_style_text_color(
          sl_val[s], foc ? current_theme.border_accent : current_theme.border_inactive, 0);
  }
}

static void sd_disarm(void) {
  if (!s_sd_armed)
    return;
  s_sd_armed = 0;
  if (badge_lbl[BADGE_SD])
    lv_label_set_text(badge_lbl[BADGE_SD], "Eject");
}

static void set_slider(int s, int v) {
  if (s < 0 || s >= SLIDER_COUNT || !sl_fill[s])
    return;
  if (v < 0)
    v = 0;
  if (v > 100)
    v = 100;
  sl_value[s] = v;
  int w = v < SLIDER_MIN_FILL_PCT ? SLIDER_MIN_FILL_PCT : v;
  lv_obj_set_width(sl_fill[s], lv_pct(w));
  if (sl_val[s])
    lv_label_set_text_fmt(sl_val[s], "%d%%", v);
  if (s == SLIDER_SOUND) {
    audio_i2s_set_volume((uint8_t)v);
    g_config_system.volume = v;
  }
}

static void slide_anim_cb(void *var, int32_t v) {
  lv_obj_set_y((lv_obj_t *)var, v);
}

static void slide_done_cb(lv_anim_t *a) {
  (void)a;
  slide_animating = false;
  if (!slide_open) {
    lv_obj_add_flag(slide_panel, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < hide_objs_count; i++)
      if (hide_objs_ref[i])
        lv_obj_remove_flag(hide_objs_ref[i], LV_OBJ_FLAG_HIDDEN);
    // Fully closed: hand input back to the screen underneath, but swallow the
    // press that closed us so it doesn't also act on that screen.
    lv_port_indev_set_suppressed(false);
    ui_input_lock(250);
  }
}

static void dropdown_open(void) {
  if (!slide_panel || slide_animating || slide_open)
    return;
  slide_animating = true;
  slide_open = true;

  // Take exclusive input: swallow keypad keys so the screen underneath (menus/
  // lists via the LVGL group) stops navigating while the panel is up.
  lv_port_indev_set_suppressed(true);

  focus_row = ROW_BADGES;
  badge_sel = 0;
  sd_disarm();
  badge_on[BADGE_WIFI] = g_config_wifi.enabled;
  badge_on[BADGE_BLE] = g_config_ble.enabled;
  s_sd_present_last = badge_is_enabled(BADGE_SD);
  set_slider(SLIDER_SOUND, g_config_system.volume);
  refresh_sd_status();
  battery_tick();
  refresh_focus();

  lv_obj_remove_flag(slide_panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(slide_panel);
  for (int i = 0; i < hide_objs_count; i++)
    if (hide_objs_ref[i])
      lv_obj_add_flag(hide_objs_ref[i], LV_OBJ_FLAG_HIDDEN);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, slide_panel);
  lv_anim_set_exec_cb(&a, slide_anim_cb);
  lv_anim_set_values(&a, -s_panel_h, 0);
  lv_anim_set_duration(&a, SLIDE_ANIM_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_completed_cb(&a, slide_done_cb);
  lv_anim_start(&a);
}

static void dropdown_close(void) {
  if (!slide_panel || slide_animating || !slide_open)
    return;
  if (s_vol_dirty) {
    (void)tos_config_save(TOS_PATH_CONFIG_SYSTEM, "system");
    s_vol_dirty = false;
  }
  slide_animating = true;
  slide_open = false;

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, slide_panel);
  lv_anim_set_exec_cb(&a, slide_anim_cb);
  lv_anim_set_values(&a, 0, -s_panel_h);
  lv_anim_set_duration(&a, SLIDE_ANIM_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_completed_cb(&a, slide_done_cb);
  lv_anim_start(&a);
}

static void dropdown_reboot(void) {
  if (slide_panel) {
    lv_obj_add_flag(slide_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(slide_panel, -s_panel_h);
  }
  for (int i = 0; i < hide_objs_count; i++)
    if (hide_objs_ref[i])
      lv_obj_remove_flag(hide_objs_ref[i], LV_OBJ_FLAG_HIDDEN);
  lv_port_indev_set_suppressed(false);
  slide_open = false;
  slide_animating = false;
  reboot_ui_reboot();
}

static void wifi_apply_task(void *arg) {
  if ((bool)(intptr_t)arg)
    wifi_service_start();
  else
    wifi_service_stop();
  vTaskDelete(NULL);
}

static void ble_apply_task(void *arg) {
  if ((bool)(intptr_t)arg) {
    bluetooth_service_init();
    bluetooth_service_start();
  } else {
    bluetooth_service_stop();
  }
  vTaskDelete(NULL);
}

static void conn_set_wifi(bool on) {
  g_config_wifi.enabled = on;
  tos_config_save(TOS_PATH_CONFIG_WIFI, "wifi");
  badge_on[BADGE_WIFI] = on;
  refresh_focus();
  if (!on)
    notify(NOTIFY_WARNING, "Wi-Fi off");
  xTaskCreatePinnedToCore(wifi_apply_task,
                          "wifi_apply",
                          4096,
                          (void *)(intptr_t)on,
                          SYS_PRIO_SERVICE_LO,
                          NULL,
                          SYS_CORE_RADIO);
}

static void conn_set_ble(bool on) {
  g_config_ble.enabled = on;
  tos_config_save(TOS_PATH_CONFIG_BLE, "ble");
  badge_on[BADGE_BLE] = on;
  refresh_focus();
  if (!on)
    notify(NOTIFY_WARNING, "BLE off");
  xTaskCreatePinnedToCore(ble_apply_task,
                          "ble_apply",
                          4096,
                          (void *)(intptr_t)on,
                          SYS_PRIO_SERVICE_LO,
                          NULL,
                          SYS_CORE_RADIO);
}

static void slide_btn_timer_cb(lv_timer_t *timer) {
  if (!slide_panel || !lv_obj_is_valid(slide_panel)) {
    lv_timer_delete(timer);
    slide_btn_timer = NULL;
    return;
  }
  bool up = ui_nav_pressed(INPUT_BTN_UP), down = ui_nav_pressed(INPUT_BTN_DOWN);
  bool left = ui_nav_pressed(INPUT_BTN_LEFT), right = ui_nav_pressed(INPUT_BTN_RIGHT);
  bool ok = ok_button_is_down(), back = back_button_is_down();

  uint32_t nowt = lv_tick_get();

  // Long-press UP toggles the panel: open from a browse screen (never while input
  // is locked or on an active operation screen), or close if already open. One
  // toggle per hold. Short taps of UP fall through to the focused screen (closed)
  // or to the in-panel row nav below (open).
  if (up && !btn_up_last) {
    up_hold_start = nowt;
    up_hold_consumed = false;
  }
  if (up && !up_hold_consumed && (uint32_t)(nowt - up_hold_start) >= LONGPRESS_MS) {
    up_hold_consumed = true;
    if (slide_open) {
      dropdown_close();
    } else if (!ui_input_is_locked() && !tutorial_is_active() &&
               ui_screen_shows_chrome(ui_current_screen())) {
      dropdown_open();
    }
  }
  if (!up) {
    up_hold_consumed = false;
  }

  if (slide_open) {
    if (up && !btn_up_last && focus_row > 0) {
      sd_disarm();
      focus_row--;
      refresh_focus();
    }
    if (down && !btn_down_last && focus_row < ROW_COUNT - 1) {
      sd_disarm();
      focus_row++;
      refresh_focus();
    }
    if (left && !btn_left_last) {
      if (focus_row == ROW_BADGES) {
        sd_disarm();
        badge_step(-1);
        refresh_focus();
      } else {
        int s = focus_row - ROW_BRIGHT;
        set_slider(s, sl_value[s] - SL_STEP);
        if (s == SLIDER_SOUND)
          s_vol_dirty = true;
      }
    }
    if (right && !btn_right_last) {
      if (focus_row == ROW_BADGES) {
        sd_disarm();
        badge_step(+1);
        refresh_focus();
      } else {
        int s = focus_row - ROW_BRIGHT;
        set_slider(s, sl_value[s] + SL_STEP);
        if (s == SLIDER_SOUND)
          s_vol_dirty = true;
      }
    }
    if (ok && !btn_ok_last && focus_row == ROW_BADGES) {
      if (badge_sel == BADGE_SD) {
        if (!vfs_sdcard_is_mounted()) {
          notify(NOTIFY_WARNING, "No SD card");
        } else if (s_sd_armed) {
          sd_disarm();
          header_ui_sd_eject();
          notify(NOTIFY_WARNING, "SD card ejected");
          refresh_focus();
        } else {
          s_sd_armed = 1;
          if (badge_lbl[BADGE_SD])
            lv_label_set_text(badge_lbl[BADGE_SD], "Confirm?");
          refresh_focus();
        }
      } else if (badge_sel == BADGE_REBOOT) {
        dropdown_reboot();
      } else if (badge_sel == BADGE_WIFI) {
        conn_set_wifi(!badge_on[BADGE_WIFI]);
      } else if (badge_sel == BADGE_BLE) {
        conn_set_ble(!badge_on[BADGE_BLE]);
      }
    }
    if (back && !btn_back_last) {
      dropdown_close();
    }
  }

  // Freeze screens that poll the buttons directly while the panel is up or
  // animating (the keypad group is already frozen via indev suppression).
  if (slide_open || slide_animating) {
    ui_input_lock(SLIDE_BTN_POLL_MS * 3);
  }

  if (slide_open) {
    if ((uint32_t)(nowt - bat_last_anim) >= BAT_ANIM_MS) {
      bat_last_anim = nowt;
      battery_tick();
      refresh_sd_status(); // reflect SD insert/remove while the panel is open
      bool sd_now = badge_is_enabled(BADGE_SD);
      if (sd_now != s_sd_present_last) {
        s_sd_present_last = sd_now;
        if (!sd_now && badge_sel == BADGE_SD) {
          sd_disarm();
          badge_step(-1);
        }
        refresh_focus();
      }
    }
  }

  btn_up_last = up;
  btn_down_last = down;
  btn_left_last = left;
  btn_right_last = right;
  btn_ok_last = ok;
  btn_back_last = back;
}

static void make_badge(lv_obj_t *row, int idx, const char *sym, const char *caption) {
  lv_obj_t *cell = lv_obj_create(row);
  lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cell, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cell, 0, 0);
  lv_obj_set_style_pad_all(cell, 0, 0);
  lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(cell, 6, 0);

  lv_obj_t *dot = lv_obj_create(cell);
  lv_obj_set_size(dot, 46, 46);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(dot, 2, 0);
  lv_obj_set_style_pad_all(dot, 0, 0);

  lv_obj_t *ic = lv_label_create(dot);
  lv_label_set_text(ic, sym);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_14, 0);
  lv_obj_center(ic);

  lv_obj_t *cap = lv_label_create(cell);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);

  badge_dot[idx] = dot;
  badge_ic[idx] = ic;
  badge_lbl[idx] = cap;
}

static void make_slider(lv_obj_t *parent, int idx, const char *icon_path) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), 24);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 10, 0);

  lv_image_dsc_t *dsc = icon_path ? assets_get(icon_path) : NULL;
  if (dsc) {
    lv_obj_t *img = lv_image_create(row);
    lv_image_set_src(img, dsc);
    lv_obj_set_size(img, 22, 22);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    sl_icon[idx] = img;
  }

  lv_obj_t *track = lv_obj_create(row);
  lv_obj_set_size(track, lv_pct(100), 14);
  lv_obj_set_flex_grow(track, 1);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(track, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_radius(track, 7, 0);
  lv_obj_set_style_bg_color(track, CHIP_BG, 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(track, 1, 0);
  lv_obj_set_style_border_color(track, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_all(track, 0, 0);

  lv_obj_t *fill = lv_obj_create(track);
  lv_obj_set_size(fill, lv_pct(50), lv_pct(100));
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(fill, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_radius(fill, 7, 0);
  lv_obj_set_style_bg_color(fill, current_theme.border_interface, 0);
  lv_obj_set_style_bg_grad_color(fill, current_theme.border_accent, 0);
  lv_obj_set_style_bg_grad_dir(fill, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(fill, 0, 0);
  lv_obj_set_style_pad_all(fill, 0, 0);
  lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *knob = lv_obj_create(fill);
  lv_obj_set_size(knob, 14, 14);
  lv_obj_remove_flag(knob, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(knob, current_theme.text_main, 0);
  lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(knob, 2, 0);
  lv_obj_set_style_border_color(knob, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(knob, 8, 0);
  lv_obj_set_style_shadow_color(knob, current_theme.border_accent, 0);
  lv_obj_align(knob, LV_ALIGN_RIGHT_MID, 7, 0);
  lv_obj_move_foreground(knob);

  lv_obj_t *val = lv_label_create(row);
  lv_obj_set_width(val, 38);
  lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(val, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);

  sl_track[idx] = track;
  sl_fill[idx] = fill;
  sl_knob[idx] = knob;
  sl_val[idx] = val;
  set_slider(idx, sl_value[idx]);
}

static void make_mini(lv_obj_t *row,
                      const char *label,
                      const char *value,
                      int pct,
                      bool battery,
                      lv_obj_t **val_out,
                      lv_obj_t **fill_out) {
  (void)label;
  lv_color_t c1 = battery ? lv_color_hex(0x00E676) : lv_color_hex(0x00BCD4);
  lv_color_t c2 = battery ? lv_color_hex(0x00A651) : lv_color_hex(0x0091A7);
  const char *sym = battery ? LV_SYMBOL_BATTERY_FULL : LV_SYMBOL_SD_CARD;

  lv_obj_t *chip = lv_obj_create(row);
  lv_obj_set_size(chip, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(chip, 1);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(chip, 10, 0);
  lv_obj_set_style_bg_color(chip, CHIP_BG, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_border_color(chip, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_all(chip, 8, 0);
  lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(chip, 6, 0);

  lv_obj_t *head = lv_obj_create(chip);
  lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(head, 0, 0);
  lv_obj_set_style_pad_all(head, 0, 0);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 7, 0);

  lv_obj_t *ic = lv_label_create(head);
  lv_label_set_text(ic, sym);
  lv_obj_set_style_text_color(ic, c1, 0);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_14, 0);

  lv_obj_t *v = lv_label_create(head);
  lv_label_set_text(v, value);
  lv_obj_set_style_text_color(v, c1, 0);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);

  lv_obj_t *track = lv_obj_create(chip);
  lv_obj_set_size(track, lv_pct(100), 7);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(track, 4, 0);
  lv_obj_set_style_bg_color(track, current_theme.text_main, 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_10, 0);
  lv_obj_set_style_border_width(track, 0, 0);
  lv_obj_set_style_pad_all(track, 0, 0);
  lv_obj_set_style_clip_corner(track, true, 0);

  if (pct < 1)
    pct = 1;
  if (pct > 100)
    pct = 100;
  lv_obj_t *fill = lv_obj_create(track);
  lv_obj_set_size(fill, lv_pct(pct), lv_pct(100));
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(fill, 4, 0);
  lv_obj_set_style_bg_color(fill, c2, 0);
  lv_obj_set_style_bg_grad_color(fill, c1, 0);
  lv_obj_set_style_bg_grad_dir(fill, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(fill, 0, 0);
  lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);

  if (val_out) {
    *val_out = v;
  }
  if (fill_out) {
    *fill_out = fill;
  }
}

static void refresh_sd_status(void) {
  int used_pct = 0;
  bool present = header_ui_sd_usage(&used_pct);

  char buf[16];
  const char *val = "No SD";
  if (present) {
    snprintf(buf, sizeof(buf), "%d%%", used_pct);
    val = buf;
  }
  if (sd_chip_val) {
    lv_label_set_text(sd_chip_val, val);
  }
  if (sd_chip_fill) {
    lv_obj_set_width(sd_chip_fill, lv_pct(used_pct < 1 ? 1 : used_pct));
  }
}

// Real battery in the dropdown mini: "--" when no charger answers, else the SoC.
// Only while actually charging: bolt prefix + fill sweeps upward (charging cue).
static void battery_tick(void) {
  if (!bat_chip_val) {
    return;
  }
  battery_snapshot_t bs;
  if (!battery_service_get(&bs) || !bs.present) {
    lv_label_set_text(bat_chip_val, "--");
    if (bat_chip_fill) {
      lv_obj_set_width(bat_chip_fill, lv_pct(1));
    }
    return;
  }
  if (bs.charging) {
    // Charging: bolt prefix + fill sweeps the full range (filling animation).
    lv_label_set_text_fmt(bat_chip_val, LV_SYMBOL_CHARGE " %d%%", bs.soc);
    bat_anim_pct += 15;
    if (bat_anim_pct > 100) {
      bat_anim_pct = 0;
    }
    if (bat_chip_fill) {
      lv_obj_set_width(bat_chip_fill, lv_pct(bat_anim_pct < 1 ? 1 : bat_anim_pct));
    }
  } else {
    // Not charging (on battery or plugged-idle): just the level, no bolt.
    lv_label_set_text_fmt(bat_chip_val, "%d%%", bs.soc);
    if (bat_chip_fill) {
      lv_obj_set_width(bat_chip_fill, lv_pct(bs.soc < 1 ? 1 : bs.soc));
    }
  }
}

void dropdown_ui_create(lv_obj_t *parent) {
  slide_panel = lv_obj_create(parent);
  lv_obj_set_size(slide_panel, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_pos(slide_panel, 0, 0);
  lv_obj_remove_flag(slide_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(slide_panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(slide_panel);

  lv_obj_set_style_radius(slide_panel, 14, 0);
  lv_obj_set_style_border_side(slide_panel, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(slide_panel, 2, 0);
  lv_obj_set_style_border_color(slide_panel, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(slide_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(slide_panel, PANEL_BG, 0);
  lv_obj_set_style_bg_grad_dir(slide_panel, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_pad_hor(slide_panel, 16, 0);
  lv_obj_set_style_pad_top(slide_panel, 12, 0);
  lv_obj_set_style_pad_bottom(slide_panel, 16, 0);
  lv_obj_set_flex_flow(slide_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      slide_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(slide_panel, 13, 0);

  lv_obj_t *grab = lv_obj_create(slide_panel);
  lv_obj_set_size(grab, 46, 5);
  lv_obj_remove_flag(grab, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(grab, 3, 0);
  lv_obj_set_style_bg_color(grab, current_theme.border_inactive, 0);
  lv_obj_set_style_bg_opa(grab, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(grab, 0, 0);

  lv_obj_t *badges = lv_obj_create(slide_panel);
  lv_obj_set_size(badges, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(badges, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(badges, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_bg_opa(badges, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(badges, 0, 0);
  lv_obj_set_style_pad_all(badges, 0, 0);
  lv_obj_set_flex_flow(badges, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      badges, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  make_badge(badges, 0, LV_SYMBOL_WIFI, "Wi-Fi");
  make_badge(badges, 1, LV_SYMBOL_BLUETOOTH, "BLE");
  make_badge(badges, 2, LV_SYMBOL_SD_CARD, "Eject");
  make_badge(badges, 3, LV_SYMBOL_POWER, "Reboot");

  sl_value[SLIDER_SOUND] = g_config_system.volume;
  make_slider(slide_panel, 0, "/assets/icons/brightness_6.bin");
  make_slider(slide_panel, 1, "/assets/icons/volume_up.bin");

  lv_obj_t *mini = lv_obj_create(slide_panel);
  lv_obj_set_size(mini, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(mini, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(mini, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(mini, 0, 0);
  lv_obj_set_style_pad_all(mini, 0, 0);
  lv_obj_set_flex_flow(mini, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      mini, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(mini, 14, 0);
  make_mini(mini, "Battery", "--", 1, true, &bat_chip_val, &bat_chip_fill);
  make_mini(mini, "Storage", "--", 1, false, &sd_chip_val, &sd_chip_fill);

  lv_obj_t *hint = lv_label_create(slide_panel);
  lv_label_set_text(hint,
                    LV_SYMBOL_UP LV_SYMBOL_DOWN " Row   " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT
                                                " Adjust   BACK Close");
  lv_obj_set_style_text_color(hint, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(hint, LV_OPA_50, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);

  focus_row = ROW_BADGES;
  badge_sel = 0;
  refresh_focus();

  lv_obj_update_layout(slide_panel);
  s_panel_h = lv_obj_get_height(slide_panel);
  if (s_panel_h < 40 || s_panel_h > ui_screen_h())
    s_panel_h = (ui_screen_h() * 85) / 100;
  lv_obj_set_y(slide_panel, -s_panel_h);

  if (slide_btn_timer == NULL)
    slide_btn_timer = lv_timer_create(slide_btn_timer_cb, SLIDE_BTN_POLL_MS, NULL);

  slide_open = false;
  slide_animating = false;
}

void dropdown_ui_register_hide_objs(lv_obj_t **objs, int count) {
  hide_objs_ref = objs;
  hide_objs_count = count;
}

bool dropdown_ui_is_open(void) {
  return slide_open;
}

void dropdown_ui_raise(void) {
  if (slide_panel)
    lv_obj_move_foreground(slide_panel);
}

void dropdown_ui_global_init(void) {
  if (slide_panel) // already created
    return;
  // The top layer sits above every screen, so the single panel survives screen
  // switches and always renders on top. The caller holds the LVGL lock.
  dropdown_ui_create(lv_layer_top());
}