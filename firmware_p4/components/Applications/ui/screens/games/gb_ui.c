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

#include "gb_ui.h"

#include <dirent.h>
#include <string.h>
#include <strings.h>

#include "esp_attr.h"

#include "lvgl.h"

#include "buttons_gpio.h"
#include "gb_highboy.h"
#include "st7789.h"
#include "storage_init.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define MAX_ROMS         64
#define NAV_MS           80
#define SD_ROOT          "/sdcard"
#define MAX_DEPTH        3
#define ROM_PATH_LEN     300
#define ROM_NAME_LEN     256
#define EXT_GB_LEN       3
#define EXT_GBC_LEN      4
#define COLOR_NO_ROMS    0xDD4444
#define LIST_TOP_OFFSET  26
#define LIST_HEIGHT_TRIM 60

EXT_RAM_BSS_ATTR static char s_paths[MAX_ROMS][ROM_PATH_LEN];
EXT_RAM_BSS_ATTR static char s_names[MAX_ROMS][ROM_NAME_LEN];
static int s_count = 0;
static int s_sel = 0;
static bool s_launched = false;

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_items[MAX_ROMS];
static lv_timer_t *s_nav = NULL;
static bool s_up_last, s_dn_last, s_ok_last, s_bk_last, s_lf_last;

static bool is_rom(const char *name) {
  size_t n = strlen(name);
  return (n >= EXT_GB_LEN && strcasecmp(name + n - EXT_GB_LEN, ".gb") == 0) ||
         (n >= EXT_GBC_LEN && strcasecmp(name + n - EXT_GBC_LEN, ".gbc") == 0);
}

static void scan_dir(const char *dir, int depth) {
  if (depth > MAX_DEPTH || s_count >= MAX_ROMS)
    return;
  DIR *d = opendir(dir);
  if (d == NULL)
    return;
  struct dirent *e;
  while ((e = readdir(d)) != NULL && s_count < MAX_ROMS) {
    const char *dn = e->d_name;
    if (dn[0] == '.')
      continue;
    if (strlen(dir) + 1 + strlen(dn) >= sizeof(s_paths[0]))
      continue;
    char full[sizeof(s_paths[0])];
    strlcpy(full, dir, sizeof(full));
    strlcat(full, "/", sizeof(full));
    strlcat(full, dn, sizeof(full));
    if (e->d_type == DT_DIR) {
      scan_dir(full, depth + 1);
    } else if (is_rom(dn)) {
      strlcpy(s_paths[s_count], full, sizeof(s_paths[0]));
      strlcpy(s_names[s_count], dn, sizeof(s_names[0]));
      s_count++;
    }
  }
  closedir(d);
}

static void scan_roms(void) {
  s_count = 0;
  scan_dir(SD_ROOT, 0);
}

static void draw_sel(void) {
  for (int i = 0; i < s_count; i++) {
    bool sel = (i == s_sel);
    lv_label_set_text_fmt(s_items[i], "%s %s", sel ? ">" : " ", s_names[i]);
    lv_obj_set_style_text_color(
        s_items[i], sel ? ui_theme_get_accent() : current_theme.text_main, 0);
  }
  if (s_count > 0)
    lv_obj_scroll_to_view(s_items[s_sel], LV_ANIM_OFF);
}

static void nav_tick(lv_timer_t *t) {
  if (lv_screen_active() != s_scr) {
    lv_timer_delete(t);
    s_nav = NULL;
    return;
  }
  if (s_launched) {
    if (highboy_gb_finished()) {
      s_launched = false;
      lcd_set_rotation(lcd_get_rotation());
      ui_switch_screen(SCREEN_GAMES_MENU);
    }
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up(), dn = ui_btn_down(), lf = ui_btn_left();
  bool ok = ok_button_is_down(), bk = back_button_is_down();

  if (s_count > 0) {
    if (dn && !s_dn_last) {
      s_sel = (s_sel + 1) % s_count;
      draw_sel();
    }
    if (up && !s_up_last) {
      s_sel = (s_sel - 1 + s_count) % s_count;
      draw_sel();
    }
    if (ok && !s_ok_last) {
      s_launched = true;
      highboy_gb_start(s_paths[s_sel]);
    }
  }
  if ((bk && !s_bk_last) || (lf && !s_lf_last))
    ui_switch_screen(SCREEN_GAMES_MENU);

  s_up_last = up;
  s_dn_last = dn;
  s_ok_last = ok;
  s_bk_last = bk;
  s_lf_last = lf;
}

void ui_gb_open(void) {
  s_sel = 0;
  s_launched = false;
  s_up_last = s_dn_last = s_ok_last = s_bk_last = s_lf_last = false;
  s_nav = NULL;

  if (!storage_is_mounted())
    storage_init();
  scan_roms();

  s_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_scr, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(s_scr, 6, 0);

  lv_obj_t *title = lv_label_create(s_scr);
  lv_label_set_text(title, "GAME BOY  -  ROMs");
  lv_obj_set_style_text_color(title, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  if (s_count == 0) {
    lv_obj_t *m = lv_label_create(s_scr);
    lv_label_set_text(
        m, "No .gb / .gbc ROMs found.\n\nCopy games anywhere on the\nSD card and reopen.");
    lv_obj_set_style_text_color(m, lv_color_hex(COLOR_NO_ROMS), 0);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(m);
  } else {
    int vres = lv_display_get_vertical_resolution(NULL);
    s_list = lv_obj_create(s_scr);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 2, 0);
    lv_obj_set_size(s_list, LV_PCT(100), vres - LIST_HEIGHT_TRIM);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, LIST_TOP_OFFSET);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    for (int i = 0; i < s_count; i++) {
      s_items[i] = lv_label_create(s_list);
      lv_obj_set_style_text_font(s_items[i], &lv_font_montserrat_14, 0);
      lv_obj_set_width(s_items[i], LV_PCT(100));
      lv_label_set_long_mode(s_items[i], LV_LABEL_LONG_DOT);
    }
    draw_sel();
  }

  lv_obj_t *hint = lv_label_create(s_scr);
  lv_label_set_text(hint, s_count > 0 ? "UP/DN pick   OK play   BACK exit" : "BACK exit");
  lv_obj_set_style_text_color(hint, current_theme.text_main, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);

  s_nav = lv_timer_create(nav_tick, NAV_MS, NULL);
  ui_screen_load(s_scr);
}
