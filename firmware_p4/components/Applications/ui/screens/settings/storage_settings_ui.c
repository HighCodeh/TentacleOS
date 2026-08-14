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

#include "storage_settings_ui.h"

#include <stdio.h>

#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "vfs_core.h"
#include "vfs_sdcard.h"

static const char *TAG = "STORAGE_UI";

#define NAV_TIMER_MS   50
#define SD_PATH        "/sdcard"
#define ASSETS_LABEL   "assets"
#define DATA_LABEL     "storage"
#define CONTENT_W      204
#define BAR_H          9
#define G1             0x7A52D6
#define G2             0xB89AFF
#define CYAN           0x00E5D0
#define OK_COLOR       0x00E676
#define DANGER_COLOR   0xFF5470
#define ACT_EJECT      0
#define ACT_FORMAT     1
#define ACT_HEALTH     2
#define ACT_COUNT      3
#define FMT_TASK_STACK 8192
#define FMT_TASK_PRIO SYS_PRIO_SERVICE_HI

#define LIST_LEFT     6
#define LIST_TOP_Y    46
#define LIST_W        218
#define LIST_H        248
#define SB_TRACK_X    227
#define SB_TRACK_Y    54
#define SB_TRACK_LEN  232
#define SB_THUMB_H    45
#define SB_THUMB_ICON "/assets/icons/drag_indicator.bin"

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_act[ACT_COUNT];
static lv_obj_t *s_col = NULL;
static lv_obj_t *s_thumb = NULL;
static lv_obj_t *s_fmt_overlay = NULL;
static lv_timer_t *s_nav_timer = NULL;
static int s_sel = 0;
static volatile bool s_formatting = false;

static bool s_up_last, s_down_last, s_ok_last, s_back_last;

static void build_screen(void);

static void fmt_size(char *out, size_t n, uint64_t bytes) {
  const uint64_t gb = 1024ULL * 1024 * 1024;
  const uint64_t mb = 1024ULL * 1024;
  const uint64_t kb = 1024ULL;
  if (bytes >= gb) {
    uint64_t t = (bytes * 10) / gb;
    snprintf(out, n, "%llu.%llu GB", (unsigned long long)(t / 10), (unsigned long long)(t % 10));
  } else if (bytes >= mb) {
    uint64_t t = (bytes * 10) / mb;
    snprintf(out, n, "%llu.%llu MB", (unsigned long long)(t / 10), (unsigned long long)(t % 10));
  } else {
    snprintf(out, n, "%llu KB", (unsigned long long)(bytes / kb));
  }
}

static void add_meta_row(lv_obj_t *parent, const char *left, const char *right, lv_color_t rc) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *l = lv_label_create(row);
  lv_label_set_text(l, left);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(l, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(l, LV_OPA_60, 0);

  lv_obj_t *r = lv_label_create(row);
  lv_label_set_text(r, right);
  lv_obj_set_style_text_font(r, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(r, rc, 0);
}

static void add_volume(lv_obj_t *parent,
                       const char *icon,
                       const char *name,
                       const char *tag,
                       uint64_t used,
                       uint64_t total) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, CONTENT_W, LV_SIZE_CONTENT);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_radius(card, 11, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_style_pad_row(card, 6, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *hdr = lv_obj_create(card);
  lv_obj_remove_style_all(hdr);
  lv_obj_set_size(hdr, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(hdr, 7, 0);

  lv_image_dsc_t *dsc = assets_get(icon);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(hdr);
    lv_image_set_src(img, dsc);
    lv_obj_set_size(img, 18, 18);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
  }
  lv_obj_t *nm = lv_label_create(hdr);
  lv_label_set_text(nm, name);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(nm, current_theme.text_main, 0);
  lv_obj_set_flex_grow(nm, 1);
  if (tag != NULL) {
    lv_obj_t *tg = lv_label_create(hdr);
    lv_label_set_text(tg, tag);
    lv_obj_set_style_text_font(tg, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(tg, lv_color_hex(CYAN), 0);
  }

  int pct = (total > 0) ? (int)((used * 100) / total) : 0;
  if (pct > 100)
    pct = 100;
  lv_obj_t *bar = lv_obj_create(card);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, lv_pct(100), BAR_H);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(bar, 5, 0);
  lv_obj_set_style_bg_color(bar, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

  lv_obj_t *fill = lv_obj_create(bar);
  lv_obj_remove_style_all(fill);
  lv_obj_set_size(fill, lv_pct(pct < 4 ? 4 : pct), lv_pct(100));
  lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(fill, 5, 0);
  lv_obj_set_style_bg_color(fill, lv_color_hex(G1), 0);
  lv_obj_set_style_bg_grad_color(fill, lv_color_hex(G2), 0);
  lv_obj_set_style_bg_grad_dir(fill, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);

  char lu[24], lf[24];
  fmt_size(lu, sizeof(lu), used);
  fmt_size(lf, sizeof(lf), total > used ? total - used : 0);
  char used_s[32], free_s[32];
  snprintf(used_s, sizeof(used_s), "%s used", lu);
  snprintf(free_s, sizeof(free_s), "%s free", lf);
  add_meta_row(card, used_s, free_s, lv_color_hex(CYAN));
}

static lv_obj_t *make_action(lv_obj_t *parent, const char *icon, const char *label, bool danger) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, CONTENT_W, 30);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(row, 9, 0);
  lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_80, 0);
  lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_border_color(
      row, danger ? lv_color_hex(DANGER_COLOR) : current_theme.border_inactive, 0);
  lv_obj_set_style_pad_left(row, 10, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);

  lv_image_dsc_t *dsc = assets_get(icon);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(row);
    lv_image_set_src(img, dsc);
    lv_obj_set_size(img, 16, 16);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
  }
  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(
      lbl, danger ? lv_color_hex(DANGER_COLOR) : current_theme.text_main, 0);
  return row;
}

static void move_thumb(void) {
  if (s_thumb == NULL || s_col == NULL)
    return;
  int sy = lv_obj_get_scroll_y(s_col);
  int total = sy + lv_obj_get_scroll_bottom(s_col);
  int thumb_h = lv_obj_get_height(s_thumb);
  if (thumb_h <= 0)
    thumb_h = SB_THUMB_H;
  int travel = SB_TRACK_LEN - thumb_h;
  if (travel < 0)
    travel = 0;
  int pos = SB_TRACK_Y;
  if (total > 0)
    pos = SB_TRACK_Y + (int)((long)sy * travel / total);
  lv_obj_set_y(s_thumb, pos);
}

static void update_selection(void) {
  for (int i = 0; i < ACT_COUNT; i++) {
    if (s_act[i] == NULL)
      continue;
    bool sel = (i == s_sel);
    lv_obj_set_style_border_color(
        s_act[i],
        sel ? current_theme.border_accent
            : (i == ACT_FORMAT ? lv_color_hex(DANGER_COLOR) : current_theme.border_inactive),
        0);
    lv_obj_set_style_bg_opa(s_act[i], sel ? LV_OPA_COVER : LV_OPA_80, 0);
  }
  if (s_col != NULL && s_act[s_sel] != NULL) {
    lv_obj_update_layout(s_col);
    lv_obj_scroll_to_view(s_act[s_sel], LV_ANIM_OFF);
  }
  move_thumb();
}

static void show_fmt_overlay(void) {
  if (s_fmt_overlay != NULL)
    return;
  s_fmt_overlay = lv_obj_create(s_screen);
  lv_obj_set_size(s_fmt_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_center(s_fmt_overlay);
  lv_obj_remove_flag(s_fmt_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_fmt_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_fmt_overlay, LV_OPA_80, 0);
  lv_obj_set_style_border_width(s_fmt_overlay, 0, 0);

  lv_obj_t *box = lv_obj_create(s_fmt_overlay);
  lv_obj_set_size(box, 190, 96);
  lv_obj_center(box);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(box, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(box, 14, 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_border_color(box, current_theme.border_accent, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(box, 10, 0);
  lv_obj_set_style_pad_row(box, 6, 0);

  lv_obj_t *t = lv_label_create(box);
  lv_label_set_text(t, LV_SYMBOL_SD_CARD "  FORMATTING");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, current_theme.border_accent, 0);

  lv_obj_t *m = lv_label_create(box);
  lv_label_set_text(m, "Do not remove the card.");
  lv_obj_set_style_text_font(m, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(m, current_theme.text_main, 0);
}

static void format_done_cb(void *data) {
  esp_err_t r = (esp_err_t)(intptr_t)data;
  s_formatting = false;
  if (s_fmt_overlay != NULL) {
    lv_obj_del(s_fmt_overlay);
    s_fmt_overlay = NULL;
  }
  if (r == ESP_OK)
    notify(NOTIFY_SAVED, "SD formatted (FAT32)");
  else
    notify(NOTIFY_WARNING, "Format failed");
  build_screen();
}

static void format_task(void *arg) {
  (void)arg;
  esp_err_t r = vfs_sdcard_format();
  if (r == ESP_OK && !vfs_sdcard_is_mounted())
    vfs_sdcard_init();
  lv_async_call(format_done_cb, (void *)(intptr_t)r);
  vTaskDelete(NULL);
}

static void fmt_confirm_cb(bool confirm) {
  if (!confirm || s_formatting)
    return;
  s_formatting = true;
  show_fmt_overlay();
  xTaskCreatePinnedToCore(format_task, "sd_format", FMT_TASK_STACK, NULL, FMT_TASK_PRIO, NULL, SYS_CORE_RADIO);
}

static void fire_action(int idx) {
  if (idx == ACT_HEALTH) {
    ui_feedback(UI_FB_SELECT);
    ui_switch_screen(SCREEN_SD_HEALTH);
    return;
  }
  if (!vfs_sdcard_is_mounted()) {
    notify(NOTIFY_WARNING, "No SD card");
    return;
  }
  if (idx == ACT_EJECT) {
    vfs_sdcard_deinit();
    ui_feedback(UI_FB_SELECT);
    notify(NOTIFY_INFO, "SD card ejected");
    build_screen();
  } else if (idx == ACT_FORMAT) {
    msgbox_open("/assets/icons/warning.bin",
                "Format SD as FAT32?\nAll files will be erased.",
                "FORMAT",
                "Cancel",
                fmt_confirm_cb);
  }
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;
  if (s_formatting || msgbox_is_open())
    return;

  bool up = ui_btn_up(), down = ui_btn_down();
  bool ok = ok_button_is_down(), back = back_button_is_down();

  if (back && !s_back_last) {
    ui_switch_screen(SCREEN_SETTINGS);
    return;
  }
  if (down && !s_down_last) {
    s_sel = (s_sel + 1) % ACT_COUNT;
    update_selection();
    ui_feedback(UI_FB_NAV);
  }
  if (up && !s_up_last) {
    s_sel = (s_sel == 0) ? ACT_COUNT - 1 : s_sel - 1;
    update_selection();
    ui_feedback(UI_FB_NAV);
  }
  if (ok && !s_ok_last)
    fire_action(s_sel);

  s_up_last = up;
  s_down_last = down;
  s_ok_last = ok;
  s_back_last = back;
}

static void build_screen(void) {
  lv_obj_t *prev = s_screen;
  (void)TAG;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "STORAGE", "/assets/icons/storage.bin");
  ui_chrome_footer(s_screen,
                   LV_SYMBOL_UP LV_SYMBOL_DOWN " Nav   " LV_SYMBOL_OK " Run   " LV_SYMBOL_LEFT
                                               " Back");

  lv_obj_t *col = lv_obj_create(s_screen);
  s_col = col;
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, LIST_W, LIST_H);
  lv_obj_align(col, LV_ALIGN_TOP_LEFT, LIST_LEFT, LIST_TOP_Y);
  lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(col, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_pad_all(col, 2, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(col, 8, 0);

  if (vfs_sdcard_is_mounted()) {
    vfs_statvfs_t st = {0};
    uint64_t total = 0, used = 0;
    if (vfs_statvfs(SD_PATH, &st) == ESP_OK) {
      total = st.total_bytes;
      used = st.used_bytes;
    }
    char name[24];
    const char *nm = vfs_sdcard_get_name(name, sizeof(name)) && name[0] ? name : "SD Card";
    add_volume(col, "/assets/icons/sd_card.bin", nm, "FAT32", used, total);
  } else {
    lv_obj_t *card = lv_obj_create(col);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, CONTENT_W, 44);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_radius(card, 11, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
    lv_obj_t *l = lv_label_create(card);
    lv_label_set_text(l, LV_SYMBOL_SD_CARD "  No SD card");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, current_theme.text_main, 0);
    lv_obj_set_style_text_opa(l, LV_OPA_60, 0);
    lv_obj_center(l);
  }

  size_t at = 0, au = 0, dt = 0, du = 0;
  esp_littlefs_info(ASSETS_LABEL, &at, &au);
  esp_littlefs_info(DATA_LABEL, &dt, &du);
  add_volume(col,
             "/assets/icons/developer_board.bin",
             "Internal",
             "flash",
             (uint64_t)au + du,
             (uint64_t)at + dt);

  s_act[ACT_EJECT] = make_action(col, "/assets/icons/eject.bin", "Eject SD", false);
  s_act[ACT_FORMAT] = make_action(col, "/assets/icons/warning.bin", "Format SD", true);
  s_act[ACT_HEALTH] = make_action(col, "/assets/icons/troubleshoot.bin", "SD Health", false);

  static lv_point_precise_t sb_pts[2];
  sb_pts[0].x = 0;
  sb_pts[0].y = 0;
  sb_pts[1].x = 0;
  sb_pts[1].y = SB_TRACK_LEN;
  lv_obj_t *track = lv_line_create(s_screen);
  lv_line_set_points(track, sb_pts, 2);
  lv_obj_set_pos(track, SB_TRACK_X, SB_TRACK_Y);
  lv_obj_set_style_line_color(track, current_theme.border_inactive, 0);
  lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_line_width(track, 3, 0);
  lv_obj_set_style_line_dash_width(track, 4, 0);
  lv_obj_set_style_line_dash_gap(track, 4, 0);

  lv_image_dsc_t *thumb_dsc = assets_get(SB_THUMB_ICON);
  s_thumb = lv_image_create(s_screen);
  if (thumb_dsc != NULL)
    lv_image_set_src(s_thumb, thumb_dsc);
  lv_obj_set_pos(s_thumb, SB_TRACK_X - 4, SB_TRACK_Y);
  lv_obj_move_foreground(s_thumb);

  lv_obj_update_layout(col);
  update_selection();

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
  if (prev != NULL)
    lv_obj_del(prev);
}

void ui_storage_settings_open(void) {
  s_sel = 0;
  s_formatting = false;
  s_fmt_overlay = NULL;
  s_col = NULL;
  s_thumb = NULL;
  s_up_last = s_down_last = s_ok_last = s_back_last = false;
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  build_screen();
}
