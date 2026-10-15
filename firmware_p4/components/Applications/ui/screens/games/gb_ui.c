// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gb_ui.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_attr.h"
#include "gb_highboy.h"
#include "game_cover.h"
#include "input_manager.h"
#include "msgbox_ui.h"
#include "st7789.h"
#include "storage_init.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"

#define MAX_ROMS     64
#define ROM_PATH_LEN 300
#define PURPLE       0xBD82FF
#define MUTED        0x9993AA
EXT_RAM_BSS_ATTR static char s_paths[MAX_ROMS][ROM_PATH_LEN];
static int s_count, s_sel;
static bool s_launched, s_truncated, s_wait_release;
static bool s_controls_seen;
static lv_obj_t *s_controls_panel;
static lv_obj_t *s_scr, *s_art, *s_fallback, *s_title, *s_counter, *s_meta, *s_list;
static lv_obj_t *s_rows[3];
static lv_timer_t *s_poll;
static int s_art_w, s_art_h;

static const char *base_name(const char *path) {
  const char *base = strrchr(path, '/');
  return base ? base + 1 : path;
}

static void display_name(char *out, size_t size, const char *path) {
  strlcpy(out, base_name(path), size);
  char *dot = strrchr(out, '.');
  if (dot)
    *dot = '\0';
  for (char *p = out; *p; ++p)
    if (*p == '_')
      *p = ' ';
}

static bool is_rom(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot && (!strcasecmp(dot, ".gb") || !strcasecmp(dot, ".gbc"));
}

static void scan_dir(const char *dir, int depth) {
  if (depth > 3)
    return;
  if (s_count >= MAX_ROMS) {
    s_truncated = true;
    return;
  }
  DIR *d = opendir(dir);
  if (!d)
    return;
  struct dirent *entry;
  while ((entry = readdir(d)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    char path[ROM_PATH_LEN];
    int n = snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
    if (n < 0 || (size_t)n >= sizeof(path))
      continue;
    struct stat st;
    if (stat(path, &st) != 0)
      continue;
    if (S_ISDIR(st.st_mode)) {
      // Avoid a long walk through the separate IR/capture/cover asset trees.
      if (!strcmp(entry->d_name, "covers") || !strcmp(entry->d_name, "infrared") ||
          !strcmp(entry->d_name, "assets") || !strcmp(entry->d_name, "captures"))
        continue;
      scan_dir(path, depth + 1);
    } else if (S_ISREG(st.st_mode) && is_rom(entry->d_name)) {
      bool duplicate = false;
      for (int i = 0; i < s_count; ++i)
        if (!strcmp(s_paths[i], path)) {
          duplicate = true;
          break;
        }
      if (duplicate)
        continue;
      if (s_count >= MAX_ROMS) {
        s_truncated = true;
        break;
      }
      strlcpy(s_paths[s_count++], path, ROM_PATH_LEN);
    }
  }
  closedir(d);
}

static void scan_roms(void) {
  s_count = 0;
  s_truncated = false;
  scan_dir("/sdcard/games/gameboy", 0);
  scan_dir("/sdcard/roms", 0);
  scan_dir("/sdcard", 0);
  // Bounded insertion sort keeps the preferred-path scan deterministic too.
  char tmp[ROM_PATH_LEN];
  for (int i = 1; i < s_count; ++i) {
    strlcpy(tmp, s_paths[i], sizeof(tmp));
    int j = i;
    while (j > 0 && strcasecmp(base_name(s_paths[j - 1]), base_name(tmp)) > 0) {
      memcpy(s_paths[j], s_paths[j - 1], ROM_PATH_LEN);
      --j;
    }
    memcpy(s_paths[j], tmp, ROM_PATH_LEN);
  }
  if (s_sel >= s_count)
    s_sel = 0;
}

static void draw_selection(void) {
  if (!s_count)
    return;
  char name[256];
  display_name(name, sizeof(name), s_paths[s_sel]);
  lv_label_set_text(s_title, name);
  lv_label_set_text_fmt(s_counter, "%d / %d%s", s_sel + 1, s_count, s_truncated ? "+" : "");
  struct stat st;
  if (!stat(s_paths[s_sel], &st))
    lv_label_set_text_fmt(s_meta, "%ld KB  /  DMG core", (long)(st.st_size / 1024));
  else
    lv_label_set_text(s_meta, "File unavailable / check SD");
  bool art = game_cover_show_rom(s_art, s_paths[s_sel], s_art_w, s_art_h);
  if (art)
    lv_obj_add_flag(s_fallback, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(s_fallback, LV_OBJ_FLAG_HIDDEN);
  for (int row = 0; row < 3; ++row) {
    int idx = s_sel + row - 1;
    if (idx < 0 || idx >= s_count) {
      lv_label_set_text(s_rows[row], "");
    } else {
      display_name(name, sizeof(name), s_paths[idx]);
      lv_label_set_text_fmt(s_rows[row], "%s %s", row == 1 ? LV_SYMBOL_RIGHT : " ", name);
    }
    lv_obj_set_style_bg_opa(s_rows[row], row == 1 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s_rows[row], lv_color_hex(row == 1 ? 0xF4EEF8 : MUTED), 0);
  }
}

static void launch_selected(void) {
  game_cover_release();
  s_launched = true;
  highboy_gb_start(s_paths[s_sel]);
}

static void show_controls(void) {
  s_controls_panel = lv_obj_create(s_scr);
  lv_obj_set_size(s_controls_panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(s_controls_panel, 0, 0);
  lv_obj_set_style_bg_color(s_controls_panel, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_controls_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_controls_panel, 0, 0);
  lv_obj_set_style_radius(s_controls_panel, 0, 0);
  lv_obj_set_style_pad_all(s_controls_panel, 16, 0);
  lv_obj_remove_flag(s_controls_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *title = lv_label_create(s_controls_panel);
  lv_label_set_text(title, "GAME BOY CONTROLS");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(PURPLE), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t *body = lv_label_create(s_controls_panel);
  lv_label_set_text(body,
                    "OK = A     BACK = B\n\n"
                    "START: OK + BACK + RIGHT\n"
                    "SELECT: OK + BACK + LEFT\n"
                    "Use physical button labels.\n\n"
                    "Exit: hold OK + BACK for 2s\n"
                    "with every D-pad button released.");
  lv_obj_set_width(body, LV_PCT(100));
  lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(body, lv_color_hex(0xF4EEF8), 0);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 35);
  lv_obj_t *hint = lv_label_create(s_controls_panel);
  lv_label_set_text(hint, "OK play     BACK cancel");
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(PURPLE), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  ui_input_lock(400);
}

static void screen_deleted(lv_event_t *event) {
  (void)event;
  if (s_poll) {
    lv_timer_delete(s_poll);
    s_poll = NULL;
  }
}

static void finish_tick(lv_timer_t *timer) {
  (void)timer;
  if (s_wait_release && !input_is_down(INPUT_BTN_UP) && !input_is_down(INPUT_BTN_DOWN) &&
      !input_is_down(INPUT_BTN_LEFT) && !input_is_down(INPUT_BTN_RIGHT) &&
      !input_is_down(INPUT_BTN_OK) && !input_is_down(INPUT_BTN_BACK))
    s_wait_release = false;
  if (s_launched && highboy_gb_finished()) {
    s_launched = false;
    s_wait_release = true;
    lcd_set_rotation(lcd_get_rotation());
    lv_obj_invalidate(s_scr);
    draw_selection();
    ui_input_lock(400);
    const char *error = highboy_gb_last_error();
    if (error && error[0])
      msgbox_open_info(NULL, "Game stopped", error, lv_color_hex(PURPLE));
  }
}

static void gb_input(const input_event_t *event, void *ctx) {
  (void)ctx;
  if (s_launched || s_wait_release)
    return;
  bool press = event->action == INPUT_ACTION_PRESS;
  bool nav = press || event->action == INPUT_ACTION_REPEAT;
  if (s_controls_panel) {
    if (press && (event->button == INPUT_BTN_OK || event->button == INPUT_BTN_BACK)) {
      lv_obj_delete(s_controls_panel);
      s_controls_panel = NULL;
      if (event->button == INPUT_BTN_OK) {
        s_controls_seen = true;
        launch_selected();
      }
    }
    return;
  }
  if (event->button == INPUT_BTN_BACK && press) {
    ui_switch_screen(SCREEN_GAMES_MENU);
  } else if (s_count && nav && (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_LEFT)) {
    s_sel = (s_sel + s_count - 1) % s_count;
    ui_feedback(UI_FB_NAV);
    draw_selection();
  } else if (s_count && nav &&
             (event->button == INPUT_BTN_DOWN || event->button == INPUT_BTN_RIGHT)) {
    s_sel = (s_sel + 1) % s_count;
    ui_feedback(UI_FB_NAV);
    draw_selection();
  } else if (s_count && press && event->button == INPUT_BTN_OK) {
    char reason[112];
    if (!highboy_gb_validate_rom(s_paths[s_sel], reason, sizeof(reason))) {
      msgbox_open_info(NULL, "Cannot open game", reason, lv_color_hex(PURPLE));
      return;
    }
    if (!s_controls_seen)
      show_controls();
    else
      launch_selected();
  }
}

static lv_obj_t *label_at(lv_obj_t *parent, int x, int y, int width, uint32_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}

void ui_gb_open(void) {
  if (s_scr) {
    lv_obj_delete(s_scr);
    s_scr = NULL;
  }
  s_launched = false;
  s_wait_release = false;
  s_controls_seen = false;
  s_controls_panel = NULL;
  if (!storage_is_mounted())
    storage_init();
  scan_roms();
  s_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_scr, 0, 0);
  lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
  ui_chrome_header(s_scr, "GAME BOY", NULL);
  ui_chrome_footer(s_scr,
                   s_count ? "UP/DN browse   OK play   BACK" : "BACK  /  Insert SD and reopen");
  int w = lv_display_get_horizontal_resolution(NULL);
  int h = lv_display_get_vertical_resolution(NULL);
  bool wide = w > h;
  if (!s_count) {
    lv_obj_t *empty = label_at(s_scr, 20, 82, w - 40, 0xF4EEF8);
    lv_label_set_text(
        empty,
        storage_is_mounted()
            ? "Your library is empty\n\nAdd your .gb games to\n/sdcard/games/gameboy\n\nGBC-only "
              "titles are not supported."
            : "SD card not available\n\nInsert your game card,\nthen reopen Game Boy.");
  } else {
    int panel_w = wide ? 144 : 146;
    int panel_h = wide ? 142 : 114;
    lv_obj_t *panel = lv_obj_create(s_scr);
    lv_obj_set_pos(panel, wide ? 10 : 47, 52);
    lv_obj_set_size(panel, panel_w, panel_h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0D0914), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x47305E), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    s_art_w = panel_w - 8;
    s_art_h = panel_h - 8;
    s_art = game_cover_create(panel);
    s_fallback = lv_label_create(panel);
    lv_label_set_text(s_fallback, "GAME\nBOY");
    lv_obj_set_style_text_font(s_fallback, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_fallback, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_fallback, lv_color_hex(PURPLE), 0);
    lv_obj_center(s_fallback);
    int text_x = wide ? 166 : 14;
    int text_w = wide ? w - 180 : w - 28;
    s_title = label_at(s_scr, text_x, wide ? 54 : 175, text_w, 0xF4EEF8);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    s_meta = label_at(s_scr, text_x, wide ? 80 : 196, text_w, MUTED);
    s_counter = label_at(s_scr, wide ? 12 : w - 80, wide ? 199 : 31, wide ? 140 : 68, PURPLE);
    if (!wide)
      lv_obj_set_style_text_align(s_counter, LV_TEXT_ALIGN_RIGHT, 0);
    s_list = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, text_x, wide ? 119 : 219);
    lv_obj_set_size(s_list, text_w, 72);
    lv_obj_remove_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 3; ++i) {
      s_rows[i] = label_at(s_list, 0, i * 24, text_w, MUTED);
      lv_obj_set_height(s_rows[i], 23);
      lv_obj_set_style_pad_left(s_rows[i], 5, 0);
      lv_obj_set_style_pad_top(s_rows[i], 4, 0);
      lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(0x271638), 0);
      lv_obj_set_style_radius(s_rows[i], 4, 0);
      lv_label_set_long_mode(s_rows[i], LV_LABEL_LONG_DOT);
    }
    draw_selection();
  }
  lv_obj_add_event_cb(s_scr, screen_deleted, LV_EVENT_DELETE, NULL);
  s_poll = lv_timer_create(finish_tick, 80, NULL);
  ui_input_set_screen_handler(gb_input, NULL);
  ui_screen_load_owned(&s_scr, s_scr);
}
