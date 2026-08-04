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

#include "files_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "storage_assets.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "wav_player_ui.h"
#include "ui_theme.h"
#include "vfs_sdcard.h"

static const char *TAG = "FILES_UI";

#define HEADER_H  36
#define PATH_H    18
#define FOOTER_H  22
#define PEEK_H    72
#define CONTENT_Y (HEADER_H + PATH_H)
#define ROW_H     31
#define ROW_GAP   3
#define ROW_STEP  (ROW_H + ROW_GAP)
#define LIST_VIS  4
#define TILE_W    68
#define TILE_H    64
#define GLABEL_H  28

#define ROW_CONTENT_W   (LCD_H_RES - 16)
#define ROW_NAME_X      36
#define ROW_NAME_W_FILE (ROW_CONTENT_W - ROW_NAME_X - 52)
#define ROW_NAME_W_DIR  (ROW_CONTENT_W - ROW_NAME_X - 24)

#define NAV_TIMER_MS 50
#define OK_LONG_MS   450
#define SCROLL_STEP  36

#define COL_RAISE 0x170A28
#define COL_DIM   0x8A8594
#define COL_DIRNM 0xF0E6FF

#define MAX_ENTRIES 96
#define MAX_DEPTH   10
#define MAX_PATH    256
#define MAX_NAME    64
#define PREVIEW_MAX 2048
#define GUTTER_MAX  512
#define FULL_PATH   (MAX_PATH + MAX_NAME)
#define ROW_POOL    LIST_VIS
#define GRID_COLS   2
#define GRID_ROWS   3
#define GRID_POOL   (GRID_COLS * GRID_ROWS)

#define ASSETS_ROOT "/assets"
#define SDCARD_ROOT "/sdcard"

enum { FT_DIR = 0, FT_IR, FT_SUB, FT_NFC, FT_LOG };

typedef struct {
  char name[MAX_NAME];
  bool is_dir;
  long size;
  uint8_t type;
} entry_t;

typedef enum { VIEW_LIST = 0, VIEW_GRID } view_t;

static const char *const VOL_LABELS[] = {"Assets", "SD Card"};
static const char *const VOL_PATHS[] = {ASSETS_ROOT, SDCARD_ROOT};
#define VOL_COUNT 2
#define VOL_SD    1

static const char *ICON_OF[] = {
    [FT_DIR] = "/assets/icons/folder.bin",
    [FT_IR] = "/assets/icons/settings_remote.bin",
    [FT_SUB] = "/assets/icons/settings_input_antenna.bin",
    [FT_NFC] = "/assets/icons/nfc.bin",
    [FT_LOG] = "/assets/icons/description.bin",
};

static lv_color_t color_of(uint8_t t) {
  switch (t) {
    case FT_DIR:
      return lv_color_hex(0xFFC400);
    case FT_IR:
      return lv_color_hex(0xFF5470);
    case FT_SUB:
      return lv_color_hex(0x00E676);
    case FT_NFC:
      return lv_color_hex(0x00BCD4);
    default:
      return lv_color_hex(0x9A93A6);
  }
}
static const char *tag_of(uint8_t t) {
  switch (t) {
    case FT_DIR:
      return "DIR";
    case FT_IR:
      return "IR";
    case FT_SUB:
      return "SUB";
    case FT_NFC:
      return "NFC";
    default:
      return "TXT";
  }
}
static const char *type_desc(uint8_t t) {
  switch (t) {
    case FT_DIR:
      return "Folder";
    case FT_IR:
      return "Infrared signal";
    case FT_SUB:
      return "Sub-GHz capture";
    case FT_NFC:
      return "NFC dump";
    default:
      return "File";
  }
}

static entry_t *s_entries = NULL;
static int *s_vol_idx = NULL;
static int s_count = 0;

static char s_cwd[MAX_PATH];
static int s_depth = 0;
static int s_sel = 0;
static int s_top = 0;
static uint8_t s_sel_stack[MAX_DEPTH];
static bool s_in_viewer = false;
static bool s_resume = false;

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_timer = NULL;
static view_t s_view = VIEW_LIST;

static lv_obj_t *s_row[ROW_POOL];
static lv_obj_t *s_row_icon[ROW_POOL];
static lv_obj_t *s_row_name[ROW_POOL];
static lv_obj_t *s_row_right[ROW_POOL];

static lv_obj_t *s_tile[GRID_POOL];
static lv_obj_t *s_tile_icon[GRID_POOL];
static lv_obj_t *s_tile_name[GRID_POOL];

static lv_obj_t *s_pk_icon, *s_pk_name, *s_pk_tag, *s_pk_meta, *s_pk_snip;
static lv_obj_t *s_gl_name, *s_gl_type;
static lv_obj_t *s_vbody = NULL;

static bool s_up_last, s_down_last, s_left_last, s_right_last, s_ok_last, s_back_last;
static uint32_t s_ok_down_since = 0;
static bool s_ok_long_fired = false;
static bool s_ok_armed = false;

static char *s_vbuf = NULL;
static char *s_gbuf = NULL;

static void nav_timer_cb(lv_timer_t *t);
static void build_screen(void);

static bool files_alloc(void) {
  s_entries = malloc(sizeof(entry_t) * MAX_ENTRIES);
  s_vol_idx = malloc(sizeof(int) * MAX_ENTRIES);
  s_vbuf = malloc(PREVIEW_MAX);
  s_gbuf = malloc(GUTTER_MAX);
  return s_entries != NULL && s_vol_idx != NULL && s_vbuf != NULL && s_gbuf != NULL;
}

static void files_free(void) {
  free(s_entries);
  s_entries = NULL;
  free(s_vol_idx);
  s_vol_idx = NULL;
  free(s_vbuf);
  s_vbuf = NULL;
  free(s_gbuf);
  s_gbuf = NULL;
}

static void copy_str(char *dst, size_t n, const char *src) {
  if (n == 0) {
    return;
  }
  size_t i = 0;
  for (; i + 1 < n && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
}

static uint8_t type_from_ext(const char *name) {
  const char *dot = strrchr(name, '.');
  if (dot == NULL) {
    return FT_LOG;
  }
  if (strcasecmp(dot, ".nfc") == 0) {
    return FT_NFC;
  }
  if (strcasecmp(dot, ".sub") == 0) {
    return FT_SUB;
  }
  if (strcasecmp(dot, ".ir") == 0) {
    return FT_IR;
  }
  return FT_LOG;
}

static const char *entry_icon(int idx) {
  if (s_depth == 0) {
    return (s_vol_idx[idx] == VOL_SD) ? "/assets/icons/sd_card.bin" : "/assets/icons/folder.bin";
  }
  return ICON_OF[s_entries[idx].type];
}

static lv_color_t entry_color(int idx) {
  if (s_depth == 0) {
    return (s_vol_idx[idx] == VOL_SD) ? lv_color_hex(0x00BCD4) : lv_color_hex(0xFFC400);
  }
  return color_of(s_entries[idx].type);
}

static void fmt_size(char *out, size_t n, long bytes) {
  if (bytes >= 1048576) {
    snprintf(out, n, "%ld.%ld MB", bytes / 1048576, ((bytes % 1048576) * 10) / 1048576);
  } else if (bytes >= 1024) {
    snprintf(out, n, "%ld.%ld KB", bytes / 1024, ((bytes % 1024) * 10) / 1024);
  } else {
    snprintf(out, n, "%ld B", bytes);
  }
}

static void scan_dir(void) {
  s_count = 0;
  if (s_entries == NULL) {
    return;
  }

  if (s_depth == 0) {
    for (int k = 0; k < VOL_COUNT && s_count < MAX_ENTRIES; k++) {
      bool present = (k == VOL_SD) ? vfs_sdcard_is_mounted() : storage_assets_is_mounted();
      if (!present) {
        continue;
      }
      entry_t *e = &s_entries[s_count];
      snprintf(e->name, sizeof(e->name), "%s", VOL_LABELS[k]);
      e->is_dir = true;
      e->size = 0;
      e->type = FT_DIR;
      s_vol_idx[s_count] = k;
      s_count++;
    }
    return;
  }

  DIR *d = opendir(s_cwd);
  if (d == NULL) {
    ESP_LOGW(TAG, "opendir failed: %s", s_cwd);
    return;
  }
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && s_count < MAX_ENTRIES) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    entry_t *e = &s_entries[s_count];
    copy_str(e->name, sizeof(e->name), ent->d_name);
    e->is_dir = (ent->d_type == DT_DIR);
    e->size = 0;
    if (!e->is_dir) {
      char full[FULL_PATH];
      snprintf(full, sizeof(full), "%s/%s", s_cwd, e->name);
      struct stat st;
      if (stat(full, &st) == 0) {
        e->size = (long)st.st_size;
      }
    }
    e->type = e->is_dir ? FT_DIR : type_from_ext(e->name);
    s_count++;
  }
  closedir(d);
}

static const char *cur_name(void) {
  if (s_depth == 0) {
    return "Files";
  }
  const char *slash = strrchr(s_cwd, '/');
  return (slash != NULL && slash[1] != '\0') ? slash + 1 : s_cwd;
}

static void style_item(lv_obj_t *o, lv_color_t c, bool sel) {
  lv_obj_set_style_border_color(o, sel ? c : current_theme.border_inactive, 0);
  lv_obj_set_style_border_opa(o, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(o, sel ? lv_color_hex(COL_RAISE) : current_theme.bg_secondary, 0);
  lv_obj_set_style_shadow_width(o, sel ? 14 : 0, 0);
  lv_obj_set_style_shadow_color(o, c, 0);
  lv_obj_set_style_shadow_spread(o, sel ? -3 : 0, 0);
}

static void fill_peek_empty(void) {
  if (!s_pk_name) {
    return;
  }
  lv_label_set_text(s_pk_name, "(empty)");
  lv_label_set_text(s_pk_tag, "");
  lv_label_set_text(s_pk_meta, "No items");
  lv_label_set_text(s_pk_snip, "");
}

static void fill_peek(int idx) {
  if (!s_pk_name) {
    return;
  }
  const entry_t *e = &s_entries[idx];
  lv_color_t c = entry_color(idx);
  lv_image_dsc_t *ic = assets_get(entry_icon(idx));
  if (ic && s_pk_icon) {
    lv_image_set_src(s_pk_icon, ic);
  }
  lv_label_set_text(s_pk_name, e->name);
  lv_label_set_text(s_pk_tag, s_depth == 0 ? "VOL" : tag_of(e->type));
  lv_obj_set_style_text_color(s_pk_tag, c, 0);
  lv_obj_set_style_border_color(s_pk_tag, c, 0);

  if (e->is_dir) {
    lv_label_set_text(s_pk_meta, s_depth == 0 ? "Storage volume" : "Folder");
    if (s_depth == 0) {
      lv_label_set_text(s_pk_snip, s_vol_idx[idx] == VOL_SD ? "SD card" : "Internal flash");
    } else {
      lv_label_set_text(s_pk_snip, "");
    }
  } else {
    char sz[16];
    fmt_size(sz, sizeof(sz), e->size);
    lv_label_set_text(s_pk_meta, sz);
    lv_label_set_text(s_pk_snip, type_desc(e->type));
  }
}

static void fill_glabel(int idx) {
  if (!s_gl_name) {
    return;
  }
  const entry_t *e = &s_entries[idx];
  lv_label_set_text(s_gl_name, e->name);
  if (e->is_dir) {
    lv_label_set_text(s_gl_type, s_depth == 0 ? "Volume" : "Folder");
  } else {
    char sz[16];
    fmt_size(sz, sizeof(sz), e->size);
    lv_label_set_text(s_gl_type, sz);
  }
  lv_obj_set_style_text_color(s_gl_type, entry_color(idx), 0);
}

static void hide_obj(lv_obj_t *o) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT);
}
static void show_obj(lv_obj_t *o) {
  lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

static void populate_row(int j) {
  int idx = s_top + j;
  lv_obj_t *row = s_row[j];
  if (idx >= s_count) {
    hide_obj(row);
    return;
  }
  show_obj(row);
  const entry_t *e = &s_entries[idx];
  lv_image_dsc_t *ic = assets_get(entry_icon(idx));
  if (ic) {
    lv_image_set_src(s_row_icon[j], ic);
  }
  lv_obj_set_width(s_row_name[j], e->is_dir ? ROW_NAME_W_DIR : ROW_NAME_W_FILE);
  lv_label_set_text(s_row_name[j], e->name);
  lv_obj_set_style_text_color(
      s_row_name[j], e->is_dir ? lv_color_hex(COL_DIRNM) : current_theme.text_main, 0);
  if (e->is_dir) {
    lv_label_set_text(s_row_right[j], LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(s_row_right[j], entry_color(idx), 0);
  } else {
    char sz[16];
    fmt_size(sz, sizeof(sz), e->size);
    lv_label_set_text(s_row_right[j], sz);
    lv_obj_set_style_text_color(s_row_right[j], lv_color_hex(COL_DIM), 0);
  }
  style_item(row, entry_color(idx), idx == s_sel);
}

static void populate_tile(int j) {
  int idx = s_top + j;
  lv_obj_t *tile = s_tile[j];
  if (idx >= s_count) {
    hide_obj(tile);
    return;
  }
  show_obj(tile);
  const entry_t *e = &s_entries[idx];
  lv_image_dsc_t *ic = assets_get(entry_icon(idx));
  if (ic) {
    lv_image_set_src(s_tile_icon[j], ic);
  }
  lv_label_set_text(s_tile_name[j], e->name);
  lv_obj_set_style_text_color(
      s_tile_name[j], e->is_dir ? lv_color_hex(COL_DIRNM) : current_theme.text_main, 0);
  style_item(tile, entry_color(idx), idx == s_sel);
}

static void refresh_selection(void) {
  if (s_count == 0) {
    if (s_view == VIEW_LIST) {
      for (int j = 0; j < ROW_POOL; j++) {
        hide_obj(s_row[j]);
      }
      fill_peek_empty();
    } else {
      for (int j = 0; j < GRID_POOL; j++) {
        hide_obj(s_tile[j]);
      }
    }
    return;
  }
  if (s_sel < 0) {
    s_sel = 0;
  }
  if (s_sel >= s_count) {
    s_sel = s_count - 1;
  }

  if (s_view == VIEW_LIST) {
    if (s_sel < s_top) {
      s_top = s_sel;
    }
    if (s_sel >= s_top + LIST_VIS) {
      s_top = s_sel - LIST_VIS + 1;
    }
    int maxtop = s_count - LIST_VIS;
    if (maxtop < 0) {
      maxtop = 0;
    }
    if (s_top > maxtop) {
      s_top = maxtop;
    }
    if (s_top < 0) {
      s_top = 0;
    }
    for (int j = 0; j < ROW_POOL; j++) {
      populate_row(j);
    }
    fill_peek(s_sel);
  } else {
    int row = s_sel / GRID_COLS;
    int toprow = s_top / GRID_COLS;
    if (row < toprow) {
      toprow = row;
    }
    if (row >= toprow + GRID_ROWS) {
      toprow = row - GRID_ROWS + 1;
    }
    if (toprow < 0) {
      toprow = 0;
    }
    s_top = toprow * GRID_COLS;
    for (int j = 0; j < GRID_POOL; j++) {
      populate_tile(j);
    }
    fill_glabel(s_sel);
  }
}

static lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  return o;
}

static void flex_spacer(lv_obj_t *parent) {
  lv_obj_t *sp = lv_obj_create(parent);
  lv_obj_remove_style_all(sp);
  lv_obj_set_height(sp, 1);
  lv_obj_set_flex_grow(sp, 1);
}

static void build_header(void) {
  lv_obj_t *hdr = lv_obj_create(s_screen);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(hdr, LCD_H_RES, HEADER_H);
  lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_bg_color(hdr, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(hdr, 8, 0);
  lv_obj_set_style_pad_ver(hdr, 0, 0);
  lv_obj_set_style_border_width(hdr, 2, 0);
  lv_obj_set_style_border_color(hdr, current_theme.border_accent, 0);
  lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *chev = lv_label_create(hdr);
  lv_label_set_text(chev, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(chev, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(
      chev, s_depth > 0 ? current_theme.border_accent : current_theme.border_inactive, 0);

  lv_obj_t *name = lv_label_create(hdr);
  lv_label_set_text(name, cur_name());
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(name, current_theme.border_accent, 0);
  lv_obj_set_style_pad_left(name, 6, 0);
  lv_obj_set_flex_grow(name, 1);

  lv_obj_t *vw = lv_label_create(hdr);
  lv_label_set_text(vw, s_view == VIEW_LIST ? "LIST" : "GRID");
  lv_obj_set_style_text_font(vw, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(vw, current_theme.border_accent, 0);
  lv_obj_set_style_bg_color(vw, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(vw, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(vw, 1, 0);
  lv_obj_set_style_border_color(vw, current_theme.border_interface, 0);
  lv_obj_set_style_radius(vw, 6, 0);
  lv_obj_set_style_pad_hor(vw, 6, 0);
  lv_obj_set_style_pad_ver(vw, 1, 0);
}

static void build_pathbar(void) {
  lv_obj_t *bar = lv_obj_create(s_screen);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(bar, LCD_H_RES, PATH_H);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, HEADER_H);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x0A0710), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_hor(bar, 10, 0);
  lv_obj_set_style_pad_ver(bar, 0, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *lbl = lv_label_create(bar);
  lv_label_set_text(lbl, s_depth == 0 ? "root" : s_cwd);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DIM), 0);
  lv_obj_set_flex_grow(lbl, 1);

  lv_obj_t *dots = plain(bar);
  lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dots, 3, 0);
  for (int k = 0; k < 4; k++) {
    lv_obj_t *dot = plain(dots);
    lv_obj_set_size(dot, 5, 5);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(
        dot, k < s_depth ? current_theme.border_accent : current_theme.border_inactive, 0);
  }
}

static lv_obj_t *type_icon(lv_obj_t *parent, const char *path, int cell) {
  lv_image_dsc_t *ic = assets_get(path);
  lv_obj_t *img = lv_image_create(parent);
  if (ic) {
    lv_image_set_src(img, ic);
  }
  lv_obj_set_size(img, cell, cell);
  lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
  return img;
}

static lv_obj_t *make_row_pool(lv_obj_t *parent, int j) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, lv_pct(100), ROW_H);
  lv_obj_set_style_radius(row, 8, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_pad_all(row, 0, 0);

  s_row_icon[j] = type_icon(row, ICON_OF[FT_LOG], 20);
  lv_obj_align(s_row_icon[j], LV_ALIGN_LEFT_MID, 8, 0);

  s_row_name[j] = lv_label_create(row);
  lv_obj_set_width(s_row_name[j], ROW_NAME_W_FILE);
  lv_label_set_long_mode(s_row_name[j], LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(s_row_name[j], &lv_font_montserrat_14, 0);
  lv_obj_align(s_row_name[j], LV_ALIGN_LEFT_MID, ROW_NAME_X, 0);

  s_row_right[j] = lv_label_create(row);
  lv_obj_set_style_text_font(s_row_right[j], &lv_font_montserrat_12, 0);
  lv_obj_align(s_row_right[j], LV_ALIGN_RIGHT_MID, -8, 0);

  return row;
}

static void build_list(void) {
  int list_h = LCD_V_RES - CONTENT_Y - FOOTER_H - PEEK_H - 8;

  lv_obj_t *wrap = lv_obj_create(s_screen);
  lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(wrap, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(wrap, LCD_H_RES, list_h);
  lv_obj_align(wrap, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y);
  lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wrap, 0, 0);
  lv_obj_set_style_pad_hor(wrap, 8, 0);
  lv_obj_set_style_pad_ver(wrap, 4, 0);
  lv_obj_set_style_pad_row(wrap, ROW_GAP, 0);
  lv_obj_set_style_clip_corner(wrap, true, 0);
  lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);

  for (int j = 0; j < ROW_POOL; j++) {
    s_row[j] = make_row_pool(wrap, j);
  }
}

static lv_obj_t *make_tile_pool(lv_obj_t *parent, int j) {
  lv_obj_t *tile = lv_obj_create(parent);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(tile, TILE_W, TILE_H);
  lv_obj_set_style_radius(tile, 11, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tile, 2, 0);
  lv_obj_set_style_pad_all(tile, 4, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(tile, 4, 0);

  s_tile_icon[j] = type_icon(tile, ICON_OF[FT_LOG], 28);

  s_tile_name[j] = lv_label_create(tile);
  lv_obj_set_width(s_tile_name[j], TILE_W - 12);
  lv_label_set_long_mode(s_tile_name[j], LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(s_tile_name[j], LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_tile_name[j], &lv_font_montserrat_12, 0);

  return tile;
}

static void build_grid(void) {
  int grid_h = LCD_V_RES - CONTENT_Y - FOOTER_H - GLABEL_H;

  lv_obj_t *g = lv_obj_create(s_screen);
  lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(g, LCD_H_RES, grid_h);
  lv_obj_align(g, LV_ALIGN_TOP_LEFT, 0, CONTENT_Y);
  lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g, 0, 0);
  lv_obj_set_style_pad_all(g, 8, 0);
  lv_obj_set_style_clip_corner(g, true, 0);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(g, 6, 0);
  lv_obj_set_style_pad_column(g, 6, 0);

  for (int j = 0; j < GRID_POOL; j++) {
    s_tile[j] = make_tile_pool(g, j);
  }

  lv_obj_t *gl = lv_obj_create(s_screen);
  lv_obj_remove_flag(gl, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(gl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(gl, LCD_H_RES, GLABEL_H);
  lv_obj_align(gl, LV_ALIGN_BOTTOM_LEFT, 0, -FOOTER_H);
  lv_obj_set_style_radius(gl, 0, 0);
  lv_obj_set_style_bg_opa(gl, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(gl, 1, 0);
  lv_obj_set_style_border_color(gl, current_theme.border_inactive, 0);
  lv_obj_set_style_border_side(gl, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_pad_hor(gl, 12, 0);
  lv_obj_set_style_pad_ver(gl, 0, 0);
  lv_obj_set_flex_flow(gl, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(gl, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  s_gl_name = lv_label_create(gl);
  lv_obj_set_width(s_gl_name, 128);
  lv_label_set_long_mode(s_gl_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(s_gl_name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_gl_name, current_theme.text_main, 0);

  flex_spacer(gl);

  s_gl_type = lv_label_create(gl);
  lv_obj_set_style_text_font(s_gl_type, &lv_font_montserrat_12, 0);
  lv_obj_set_style_pad_left(s_gl_type, 8, 0);
}

static void build_peek(void) {
  lv_obj_t *pk = lv_obj_create(s_screen);
  lv_obj_remove_flag(pk, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(pk, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(pk, LCD_H_RES - 16, PEEK_H);
  lv_obj_align(pk, LV_ALIGN_BOTTOM_MID, 0, -(FOOTER_H + 4));
  lv_obj_set_style_radius(pk, 11, 0);
  lv_obj_set_style_bg_color(pk, lv_color_hex(COL_RAISE), 0);
  lv_obj_set_style_bg_grad_color(pk, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(pk, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(pk, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(pk, 1, 0);
  lv_obj_set_style_border_color(pk, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_hor(pk, 10, 0);
  lv_obj_set_style_pad_ver(pk, 6, 0);
  lv_obj_set_flex_flow(pk, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pk, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(pk, 3, 0);

  lv_obj_t *hrow = plain(pk);
  lv_obj_set_size(hrow, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(hrow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(hrow, 7, 0);

  s_pk_icon = type_icon(hrow, ICON_OF[FT_LOG], 16);

  s_pk_name = lv_label_create(hrow);
  lv_obj_set_width(s_pk_name, 118);
  lv_label_set_long_mode(s_pk_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(s_pk_name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_pk_name, current_theme.text_main, 0);

  flex_spacer(hrow);

  s_pk_tag = lv_label_create(hrow);
  lv_obj_set_style_text_font(s_pk_tag, &lv_font_montserrat_12, 0);
  lv_obj_set_style_border_width(s_pk_tag, 1, 0);
  lv_obj_set_style_radius(s_pk_tag, 5, 0);
  lv_obj_set_style_pad_hor(s_pk_tag, 4, 0);

  s_pk_meta = lv_label_create(pk);
  lv_label_set_long_mode(s_pk_meta, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_pk_meta, lv_pct(100));
  lv_obj_set_style_text_font(s_pk_meta, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_pk_meta, lv_color_hex(COL_DIM), 0);

  s_pk_snip = lv_label_create(pk);
  lv_label_set_long_mode(s_pk_snip, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_pk_snip, lv_pct(100));
  lv_obj_set_style_text_font(s_pk_snip, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_pk_snip, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(s_pk_snip, LV_OPA_60, 0);
}

static void build_footer(const char *hint) {
  lv_obj_t *ft = lv_obj_create(s_screen);
  lv_obj_remove_flag(ft, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(ft, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(ft, LCD_H_RES, FOOTER_H);
  lv_obj_align(ft, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_radius(ft, 0, 0);
  lv_obj_set_style_bg_color(ft, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(ft, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(ft, 0, 0);
  lv_obj_set_style_border_width(ft, 2, 0);
  lv_obj_set_style_border_color(ft, current_theme.border_interface, 0);
  lv_obj_set_style_border_side(ft, LV_BORDER_SIDE_TOP, 0);

  lv_obj_t *lbl = lv_label_create(ft);
  lv_label_set_text(lbl, hint);
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(lbl, LV_OPA_70, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(lbl);
}

static void build_viewer_header(const entry_t *e, int lines) {
  lv_obj_t *hdr = lv_obj_create(s_screen);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(hdr, LCD_H_RES, HEADER_H);
  lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_bg_color(hdr, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(hdr, 8, 0);
  lv_obj_set_style_pad_ver(hdr, 0, 0);
  lv_obj_set_style_border_width(hdr, 2, 0);
  lv_obj_set_style_border_color(hdr, current_theme.border_accent, 0);
  lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  type_icon(hdr, ICON_OF[e->type], 20);

  lv_obj_t *name = lv_label_create(hdr);
  lv_obj_set_width(name, 135);
  lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_label_set_text(name, e->name);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(name, current_theme.border_accent, 0);
  lv_obj_set_style_pad_left(name, 6, 0);

  flex_spacer(hdr);

  lv_obj_t *cnt = lv_label_create(hdr);
  lv_label_set_text_fmt(cnt, "%d ln", lines);
  lv_obj_set_style_text_font(cnt, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cnt, lv_color_hex(COL_DIM), 0);
}

static void load_preview(const char *path) {
  s_vbuf[0] = '\0';
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    snprintf(s_vbuf, PREVIEW_MAX, "(cannot open file)");
    return;
  }
  size_t n = fread(s_vbuf, 1, PREVIEW_MAX - 1, f);
  fclose(f);
  s_vbuf[n] = '\0';
  for (size_t i = 0; i < n; i++) {
    char c = s_vbuf[i];
    if (c == '\n' || c == '\r' || c == '\t') {
      continue;
    }
    if (c < 32 || c > 126) {
      s_vbuf[i] = '.';
    }
  }
}

static void build_viewer(void) {
  const entry_t *e = &s_entries[s_sel];
  char full[FULL_PATH];
  snprintf(full, sizeof(full), "%s/%s", s_cwd, e->name);
  load_preview(full);

  int n = (int)strlen(s_vbuf);
  while (n > 0 && (s_vbuf[n - 1] == '\n' || s_vbuf[n - 1] == '\r')) {
    n--;
  }
  s_vbuf[n] = '\0';
  int lines = 1;
  for (int i = 0; i < n; i++) {
    if (s_vbuf[i] == '\n') {
      lines++;
    }
  }

  int g = 0;
  for (int i = 1; i <= lines && g < GUTTER_MAX - 16; i++) {
    g += snprintf(s_gbuf + g, GUTTER_MAX - g, "%d\n", i);
  }
  if (g > 0 && s_gbuf[g - 1] == '\n') {
    s_gbuf[g - 1] = '\0';
  }

  build_viewer_header(e, lines);

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_set_size(body, LCD_H_RES, LCD_V_RES - HEADER_H - FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, HEADER_H);
  lv_obj_set_style_radius(body, 0, 0);
  lv_obj_set_style_bg_color(body, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 8, 0);
  lv_obj_set_style_pad_column(body, 10, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(body, current_theme.border_accent, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(body, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(body, 2, LV_PART_SCROLLBAR);
  s_vbody = body;

  lv_obj_t *gut = lv_label_create(body);
  lv_label_set_text(gut, s_gbuf);
  lv_obj_set_width(gut, 24);
  lv_obj_set_style_text_align(gut, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(gut, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(gut, lv_color_hex(COL_DIM), 0);

  lv_obj_t *txt = lv_label_create(body);
  lv_label_set_text(txt, s_vbuf);
  lv_obj_set_style_text_font(txt, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(txt, current_theme.text_main, 0);

  lv_obj_scroll_to_y(body, 0, LV_ANIM_OFF);

  build_footer("UP/DOWN scroll    BACK close");
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_vbody = NULL;
  s_pk_icon = s_pk_name = s_pk_tag = s_pk_meta = s_pk_snip = NULL;
  s_gl_name = s_gl_type = NULL;
  for (int j = 0; j < ROW_POOL; j++) {
    s_row[j] = NULL;
  }
  for (int j = 0; j < GRID_POOL; j++) {
    s_tile[j] = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  if (s_in_viewer) {
    build_viewer();
  } else {
    build_header();
    build_pathbar();
    if (s_view == VIEW_LIST) {
      build_list();
      build_peek();
    } else {
      build_grid();
    }
    build_footer("OK open   BACK up   hold OK: view");
    refresh_selection();
  }

  if (s_timer == NULL) {
    s_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
  }

  ui_screen_load(s_screen);
}

static void move_list(int d) {
  if (s_count <= 0) {
    return;
  }
  s_sel = (s_sel + d + s_count) % s_count;
  refresh_selection();
  ui_feedback(UI_FB_NAV);
}
static void move_grid(int dx, int dy) {
  if (s_count <= 0) {
    return;
  }
  int s = s_sel;
  if (dx > 0 && s < s_count - 1) {
    s++;
  }
  if (dx < 0 && s > 0) {
    s--;
  }
  if (dy > 0) {
    int t = s + GRID_COLS;
    s = (t < s_count) ? t : s_count - 1;
  }
  if (dy < 0) {
    int t = s - GRID_COLS;
    if (t >= 0) {
      s = t;
    }
  }
  if (s != s_sel) {
    s_sel = s;
    refresh_selection();
    ui_feedback(UI_FB_NAV);
  }
}

static void enter_dir(const char *path, bool absolute) {
  if (s_depth >= MAX_DEPTH - 1) {
    return;
  }
  s_sel_stack[s_depth] = (uint8_t)s_sel;
  if (absolute) {
    snprintf(s_cwd, sizeof(s_cwd), "%s", path);
  } else {
    size_t l = strlen(s_cwd);
    snprintf(s_cwd + l, sizeof(s_cwd) - l, "/%s", path);
  }
  s_depth++;
  s_sel = 0;
  s_top = 0;
  scan_dir();
  ui_feedback(UI_FB_SELECT);
  build_screen();
}

static void do_enter(void) {
  if (s_count <= 0) {
    return;
  }
  if (s_depth == 0) {
    enter_dir(VOL_PATHS[s_vol_idx[s_sel]], true);
    return;
  }
  const entry_t *e = &s_entries[s_sel];
  if (e->is_dir) {
    enter_dir(e->name, false);
    return;
  }
  const char *dot = strrchr(e->name, '.');
  if (dot != NULL && strcasecmp(dot, ".wav") == 0) {
    char full[FULL_PATH];
    snprintf(full, sizeof(full), "%s/%s", s_cwd, e->name);
    ui_feedback(UI_FB_SELECT);
    s_resume = true;
    ui_wav_player_set_path(full);
    ui_wav_player_set_return(SCREEN_FILES);
    ui_switch_screen(SCREEN_WAV_PLAYER);
    return;
  }
  s_in_viewer = true;
  ui_feedback(UI_FB_SELECT);
  build_screen();
}

static void do_back(void) {
  if (s_in_viewer) {
    s_in_viewer = false;
    build_screen();
    return;
  }
  if (s_depth == 0) {
    files_free();
    ui_switch_screen(SCREEN_MENU);
    return;
  }
  s_depth--;
  if (s_depth == 0) {
    s_cwd[0] = '\0';
  } else {
    char *slash = strrchr(s_cwd, '/');
    if (slash != NULL) {
      *slash = '\0';
    }
  }
  s_sel = s_sel_stack[s_depth];
  s_top = 0;
  scan_dir();
  ui_feedback(UI_FB_NAV);
  build_screen();
}

static void toggle_view(void) {
  s_view = (s_view == VIEW_LIST) ? VIEW_GRID : VIEW_LIST;
  s_top = 0;
  ui_feedback(UI_FB_SELECT);
  build_screen();
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }

  bool up = ui_btn_up(), down = ui_btn_down();
  bool left = ui_btn_left(), right = ui_btn_right();
  bool ok = ok_button_is_down(), back = back_button_is_down();
  bool ok_short = false;
  uint32_t now = lv_tick_get();

  if (ui_input_is_locked())
    goto latch;

  if (s_in_viewer) {
    if (down && !s_down_last && s_vbody)
      lv_obj_scroll_by(s_vbody, 0, -SCROLL_STEP, LV_ANIM_ON);
    if (up && !s_up_last && s_vbody)
      lv_obj_scroll_by(s_vbody, 0, SCROLL_STEP, LV_ANIM_ON);
    if ((back && !s_back_last) || (left && !s_left_last)) {
      do_back();
      goto latch;
    }
    goto latch;
  }

  if (ok && !s_ok_last) {
    s_ok_down_since = now;
    s_ok_long_fired = false;
    s_ok_armed = true;
  }
  if (ok && s_ok_last && s_ok_armed && !s_ok_long_fired && (now - s_ok_down_since) >= OK_LONG_MS) {
    s_ok_long_fired = true;
    s_ok_armed = false;
    toggle_view();
    goto latch;
  }
  ok_short = (!ok && s_ok_last && s_ok_armed && !s_ok_long_fired);

  if (s_view == VIEW_LIST) {
    if (down && !s_down_last)
      move_list(+1);
    if (up && !s_up_last)
      move_list(-1);
    if (right && !s_right_last) {
      do_enter();
      goto latch;
    }
    if ((back && !s_back_last) || (left && !s_left_last)) {
      do_back();
      goto latch;
    }
  } else {
    if (down && !s_down_last)
      move_grid(0, +1);
    if (up && !s_up_last)
      move_grid(0, -1);
    if (right && !s_right_last)
      move_grid(+1, 0);
    if (left && !s_left_last)
      move_grid(-1, 0);
    if (back && !s_back_last) {
      do_back();
      goto latch;
    }
  }

  if (ok_short) {
    s_ok_armed = false;
    do_enter();
    goto latch;
  }

latch:
  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_files_open(void) {
  ESP_LOGI(TAG, "files explorer");
  files_free();
  if (!files_alloc()) {
    ESP_LOGE(TAG, "files: out of memory");
    files_free();
  }

  s_in_viewer = false;
  s_ok_long_fired = false;
  s_ok_armed = false;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;

  if (s_resume) {
    s_resume = false;
    scan_dir();
    if (s_sel >= s_count)
      s_sel = s_count > 0 ? s_count - 1 : 0;
    build_screen();
    return;
  }

  s_depth = 0;
  s_cwd[0] = '\0';
  s_sel = 0;
  s_top = 0;
  scan_dir();
  build_screen();
}
