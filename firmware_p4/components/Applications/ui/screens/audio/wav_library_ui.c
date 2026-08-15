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

#include "wav_library_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "st7789.h"

#include "assets_manager.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wav_player_ui.h"

#define SDCARD_ROOT  "/sdcard"
#define MAX_TRACKS   64
#define PATH_LEN     192
#define NAME_LEN     56
#define MAX_DEPTH    3
#define HDR_ICON     "/assets/icons/graphic_eq.bin"
#define ROW_ICON     "/assets/icons/music_note.bin"

#define EMPTY_ICON "/assets/icons/sd_card.bin"
#define EMPTY_MSG  "No .wav on SD card"
#define EMPTY_SUB  "Add tracks and reopen"
#define CARD_W     200
#define CARD_H     132
#define BADGE_PX   56
#define ICON_PX    34
#define DIM_COLOR  0x8A8594

#define LIST_PAD_SIDE  8
#define LIST_PAD_ROW   6
#define LIST_SB_W      4
#define LIST_SB_RADIUS 2

#define ROW_H        44
#define ROW_RADIUS   10
#define ROW_PAD_HOR  10
#define ROW_PAD_COL  10
#define ROW_GLOW_W   14
#define ROW_BADGE_PX 28
#define ROW_ICON_PX  16
#define ROW_EQ_W     20
#define ROW_EQ_H     16
#define ROW_EQ_BARS  4
#define ROW_EQ_BAR_W 3
#define EQ_TICK_MS   140
#define NAME_DISP    48
#define EXT_BUF      8

#define DUR_BASE_S 120
#define DUR_STEP_S 17
#define DUR_BUF    8

#define HINT_TEXT "OK play    BACK exit"

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_rows[MAX_TRACKS];
static lv_obj_t *s_row_name[MAX_TRACKS];
static lv_obj_t *s_row_val[MAX_TRACKS];
static lv_obj_t *s_row_note[MAX_TRACKS];
static lv_obj_t *s_row_eq[MAX_TRACKS];
static lv_timer_t *s_eq_timer = NULL;
static int s_eq_phase = 0;

static const uint8_t EQ_PAT[8] = {3, 7, 11, 15, 16, 12, 8, 4};

static char s_paths[MAX_TRACKS][PATH_LEN];
static char s_names[MAX_TRACKS][NAME_LEN];
static int s_count = 0;
static int s_sel = 0;
static bool s_resume = false;
static bool s_empty = false;

static bool is_wav(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot != NULL && strcasecmp(dot, ".wav") == 0;
}

static void scan_dir(const char *dir, int depth) {
  if (depth > MAX_DEPTH || s_count >= MAX_TRACKS)
    return;
  DIR *d = opendir(dir);
  if (d == NULL)
    return;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && s_count < MAX_TRACKS) {
    const char *dn = ent->d_name;
    if (dn[0] == '.')
      continue;
    if (strlen(dir) + 1 + strlen(dn) >= PATH_LEN)
      continue;
    char full[PATH_LEN];
    strlcpy(full, dir, sizeof(full));
    strlcat(full, "/", sizeof(full));
    strlcat(full, dn, sizeof(full));
    if (ent->d_type == DT_DIR) {
      scan_dir(full, depth + 1);
    } else if (is_wav(dn)) {
      strncpy(s_paths[s_count], full, PATH_LEN - 1);
      s_paths[s_count][PATH_LEN - 1] = '\0';
      strncpy(s_names[s_count], dn, NAME_LEN - 1);
      s_names[s_count][NAME_LEN - 1] = '\0';
      s_count++;
    }
  }
  closedir(d);
}

static void scan_wavs(void) {
  s_count = 0;
  scan_dir(SDCARD_ROOT, 0);
}

int ui_wav_library_count(void) {
  return s_count;
}

const char *ui_wav_library_path(int i) {
  if (i < 0 || i >= s_count)
    return NULL;
  return s_paths[i];
}

const char *ui_wav_library_name(int i) {
  if (i < 0 || i >= s_count)
    return NULL;
  return s_names[i];
}

void ui_wav_library_set_selected(int i) {
  if (i >= 0 && i < s_count)
    s_sel = i;
}

static void track_duration(int i, char *out, size_t n) {
  int total = DUR_BASE_S + i * DUR_STEP_S;
  snprintf(out, n, "%d:%02d", total / 60, total % 60);
}

static void build_empty_card(void) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, CARD_W, CARD_H);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, (UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H) / 2);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, 18, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_style_pad_row(card, 8, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_image_dsc_t *dsc = assets_get(EMPTY_ICON);
  if (dsc != NULL) {
    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_set_size(badge, BADGE_PX, BADGE_PX);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, current_theme.border_accent, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
    lv_obj_set_style_border_width(badge, 2, 0);
    lv_obj_set_style_border_color(badge, current_theme.border_accent, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_t *img = lv_image_create(badge);
    lv_image_set_src(img, dsc);
    lv_obj_set_size(img, ICON_PX, ICON_PX);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_center(img);
  }

  lv_obj_t *msg = lv_label_create(card);
  lv_label_set_text(msg, EMPTY_MSG);
  lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(msg, current_theme.text_main, 0);

  lv_obj_t *sub = lv_label_create(card);
  lv_label_set_text(sub, EMPTY_SUB);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(DIM_COLOR), 0);
}

static void split_name(const char *full, char *base, size_t bn, char *ext, size_t en) {
  const char *dot = strrchr(full, '.');
  if (dot != NULL && dot != full) {
    size_t blen = (size_t)(dot - full);
    if (blen >= bn)
      blen = bn - 1;
    memcpy(base, full, blen);
    base[blen] = '\0';
    size_t j = 0;
    for (const char *p = dot + 1; *p != '\0' && j + 1 < en; p++) {
      char c = *p;
      if (c >= 'a' && c <= 'z')
        c = (char)(c - 32);
      ext[j++] = c;
    }
    ext[j] = '\0';
  } else {
    snprintf(base, bn, "%s", full);
    ext[0] = '\0';
  }
}

static void eq_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_eq_timer = NULL;
    return;
  }
  if (s_sel < 0 || s_sel >= s_count)
    return;
  lv_obj_t *eq = s_row_eq[s_sel];
  if (eq == NULL)
    return;
  s_eq_phase = (s_eq_phase + 1) & 7;
  uint32_t n = lv_obj_get_child_count(eq);
  for (uint32_t k = 0; k < n; k++) {
    lv_obj_t *bar = lv_obj_get_child(eq, k);
    lv_obj_set_height(bar, EQ_PAT[(s_eq_phase + k * 2) & 7]);
  }
}

static void style_row(int i, bool sel) {
  lv_obj_t *row = s_rows[i];
  if (s_row_note[i] != NULL) {
    if (sel)
      lv_obj_add_flag(s_row_note[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(s_row_note[i], LV_OBJ_FLAG_HIDDEN);
  }
  if (s_row_eq[i] != NULL) {
    if (sel)
      lv_obj_remove_flag(s_row_eq[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_row_eq[i], LV_OBJ_FLAG_HIDDEN);
  }
  if (sel) {
    lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(row, ROW_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(row, -2, 0);
    lv_obj_set_style_text_color(s_row_name[i], current_theme.text_main, 0);
    lv_obj_set_style_text_color(s_row_val[i], current_theme.border_accent, 0);
  } else {
    lv_obj_set_style_bg_color(row, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s_row_name[i], lv_color_hex(DIM_COLOR), 0);
    lv_obj_set_style_text_color(s_row_val[i], lv_color_hex(DIM_COLOR), 0);
  }
}

static void update_selection(void) {
  for (int i = 0; i < s_count; i++)
    style_row(i, i == s_sel);
  if (s_list != NULL && s_sel >= 0 && s_sel < s_count && s_rows[s_sel] != NULL) {
    lv_obj_update_layout(s_list);
    lv_obj_scroll_to_view(s_rows[s_sel], LV_ANIM_ON);
  }
}

static void build_list(void) {
  lv_color_t accent = current_theme.border_accent;

  lv_obj_t *cont = lv_obj_create(s_screen);
  s_list = cont;
  lv_obj_set_size(cont, lv_pct(100), LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_left(cont, LIST_PAD_SIDE, 0);
  lv_obj_set_style_pad_right(cont, LIST_PAD_SIDE, 0);
  lv_obj_set_style_pad_row(cont, LIST_PAD_ROW, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_ON);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_bg_color(cont, accent, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(cont, LIST_SB_W, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(cont, LIST_SB_RADIUS, LV_PART_SCROLLBAR);

  lv_image_dsc_t *glyph = assets_get(ROW_ICON);

  for (int i = 0; i < s_count; i++) {
    lv_obj_t *row = lv_obj_create(cont);
    s_rows[i] = row;
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, lv_pct(100), ROW_H);
    lv_obj_set_style_radius(row, ROW_RADIUS, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_shadow_color(row, accent, 0);
    lv_obj_set_style_pad_hor(row, ROW_PAD_HOR, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_style_pad_column(row, ROW_PAD_COL, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *badge = lv_obj_create(row);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(badge, ROW_BADGE_PX, ROW_BADGE_PX);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, accent, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, accent, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);

    s_row_note[i] = NULL;
    if (glyph != NULL) {
      lv_obj_t *img = lv_image_create(badge);
      lv_image_set_src(img, glyph);
      lv_obj_set_size(img, ROW_ICON_PX, ROW_ICON_PX);
      lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
      lv_obj_set_style_image_recolor(img, accent, 0);
      lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
      lv_obj_center(img);
      s_row_note[i] = img;
    }

    lv_obj_t *eq = lv_obj_create(badge);
    s_row_eq[i] = eq;
    lv_obj_remove_flag(eq, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(eq, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(eq, ROW_EQ_W, ROW_EQ_H);
    lv_obj_center(eq);
    lv_obj_set_style_bg_opa(eq, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(eq, 0, 0);
    lv_obj_set_style_pad_all(eq, 0, 0);
    lv_obj_set_style_pad_column(eq, 2, 0);
    lv_obj_set_flex_flow(eq, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(eq, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    for (int k = 0; k < ROW_EQ_BARS; k++) {
      lv_obj_t *bar = lv_obj_create(eq);
      lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(bar, ROW_EQ_BAR_W, EQ_PAT[(k * 2) & 7]);
      lv_obj_set_style_radius(bar, 1, 0);
      lv_obj_set_style_border_width(bar, 0, 0);
      lv_obj_set_style_bg_color(bar, accent, 0);
      lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
    lv_obj_add_flag(eq, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(col, lv_pct(100));
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 1, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    char base[NAME_DISP];
    char ext[EXT_BUF];
    split_name(s_names[i], base, sizeof(base), ext, sizeof(ext));

    lv_obj_t *name = lv_label_create(col);
    s_row_name[i] = name;
    lv_obj_set_width(name, lv_pct(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(name, base);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);

    lv_obj_t *extl = lv_label_create(col);
    lv_label_set_text(extl, ext);
    lv_obj_set_style_text_font(extl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(extl, lv_color_hex(DIM_COLOR), 0);

    char dur[DUR_BUF];
    track_duration(i, dur, sizeof(dur));
    lv_obj_t *val = lv_label_create(row);
    s_row_val[i] = val;
    lv_label_set_text(val, dur);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
  }

  update_selection();

  if (s_eq_timer == NULL)
    s_eq_timer = lv_timer_create(eq_tick_cb, EQ_TICK_MS, NULL);
}

static void play_selected(void) {
  if (s_count <= 0)
    return;
  if (s_sel < 0 || s_sel >= s_count)
    return;
  s_resume = true;
  ui_feedback(UI_FB_SELECT);
  ui_wav_player_set_path(s_paths[s_sel]);
  ui_wav_player_set_index(s_sel);
  ui_wav_player_set_return(SCREEN_PLAYER);
  ui_switch_screen(SCREEN_WAV_PLAYER);
}

static void wav_library_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    case INPUT_BTN_DOWN:
      if (nav && !s_empty && s_sel < s_count - 1) {
        s_sel++;
        update_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav && !s_empty && s_sel > 0) {
        s_sel--;
        update_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press && !s_empty)
        play_selected();
      break;
    default:
      break;
  }
}

void ui_wav_library_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_list = NULL;
  if (s_eq_timer != NULL) {
    lv_timer_delete(s_eq_timer);
    s_eq_timer = NULL;
  }
  s_eq_phase = 0;

  if (!s_resume) {
    scan_wavs();
    s_sel = 0;
  }
  s_resume = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  if (s_count <= 0) {
    s_empty = true;
    ui_chrome_header(s_screen, "PLAYER", HDR_ICON);
    build_empty_card();
    ui_chrome_footer(s_screen, "BACK exit");
  } else {
    s_empty = false;
    if (s_sel < 0)
      s_sel = 0;
    if (s_sel >= s_count)
      s_sel = s_count - 1;

    // Track count folded into the title: the header's right edge now belongs to
    // the shared status cluster, so a separate right-aligned label would collide.
    char title[24];
    snprintf(title, sizeof(title), "PLAYER (%d)", s_count);
    ui_chrome_header(s_screen, title, HDR_ICON);

    build_list();
    ui_chrome_footer(s_screen, HINT_TEXT);
  }

  ui_input_set_screen_handler(wav_library_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
