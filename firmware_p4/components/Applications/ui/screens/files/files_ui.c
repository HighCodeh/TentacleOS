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

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

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
#define ROW_NAME_W_FILE (ROW_CONTENT_W - ROW_NAME_X - 8)
#define ROW_NAME_W_DIR  (ROW_CONTENT_W - ROW_NAME_X - 46)

#define NAV_TIMER_MS 50
#define OK_LONG_MS   450
#define SCROLL_STEP  36

#define COL_RAISE 0x170A28
#define COL_DIM   0x8A8594
#define COL_DIRNM 0xF0E6FF

enum { FT_DIR = 0, FT_IR, FT_SUB, FT_NFC, FT_LOG };

typedef struct file_node {
  const char *name;
  uint8_t type;
  const struct file_node *children;
  uint8_t child_count;
  const char *meta;
  const char *snip;
  const char *content;
} file_node_t;

#define ARRLEN(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

static const char C_OFFICE_NFC[] = "Filetype: NFC device\n"
                                   "Type: Mifare Classic 1K\n"
                                   "UID: 1A 2B 3C 4D\n"
                                   "ATQA: 00 04  SAK: 08\n"
                                   "#\n"
                                   "Blk0: 1A2B3C4D 08040062\n"
                                   "Blk3: FFFFFFFFFFFF 0780\n"
                                   "# 16 sectors\n";
static const char C_TRANSIT_NFC[] = "Filetype: NFC device\n"
                                    "Type: MF Ultralight\n"
                                    "UID: 04 A3 F2 9C\n"
                                    "#\n"
                                    "Page04: 12 34 56 78\n"
                                    "Page05: 9A BC DE F0\n"
                                    "# 20 pages\n";
static const char C_MYSTERY_NFC[] = "Filetype: NFC device\n"
                                    "Type: ISO14443-3A\n"
                                    "UID: 88 04 5F 2A\n"
                                    "ATQA: 00 44  SAK: 00\n"
                                    "# unidentified\n";
static const char C_GATE_SUB[] = "Filetype: SubGhz Key\n"
                                 "Freq: 433.92 MHz\n"
                                 "Preset: Ook650Async\n"
                                 "Protocol: Princeton\n"
                                 "Bit: 24\n"
                                 "Key: 000000 1A2B3C\n"
                                 "TE: 400  Repeat: 5\n"
                                 "# end\n";
static const char C_TPMS_SUB[] = "Filetype: SubGhz RAW\n"
                                 "Freq: 315.00 MHz\n"
                                 "Protocol: TPMS\n"
                                 "#\n"
                                 "Pressure: 32 psi\n"
                                 "Temp: 24 C\n"
                                 "ID: 0x068A\n"
                                 "# end\n";
static const char C_SWEEP_TXT[] =
    "# sweep list (MHz)\n433.92\n315.00\n868.35\n915.00\n40.68\n# end\n";
static const char C_SAMSUNG_IR[] = "Filetype: IR signals\n"
                                   "Protocol: NECext\n"
                                   "#\n"
                                   "Power   cmd 08 F7\n"
                                   "Vol_up  cmd 02 FD\n"
                                   "Vol_dn  cmd 03 FC\n"
                                   "Mute    cmd 09 F6\n"
                                   "# 4 buttons\n";
static const char C_LG_IR[] = "Filetype: IR signals\n"
                              "Protocol: NEC\n"
                              "#\n"
                              "Power  cmd 08\n"
                              "CH_up  cmd 00\n"
                              "CH_dn  cmd 01\n"
                              "Menu   cmd 43\n"
                              "# 6 buttons\n";
static const char C_MIDEA_IR[] = "Filetype: IR signals\n"
                                 "Protocol: Midea (AC)\n"
                                 "#\n"
                                 "Cool 24C\n"
                                 "Fan: auto\n"
                                 "Swing: off\n"
                                 "Off\n"
                                 "# AC kit\n";
static const char C_PAYLOAD_TXT[] = "REM win recon\n"
                                    "DELAY 500\n"
                                    "GUI r\n"
                                    "DELAY 200\n"
                                    "STRING cmd\n"
                                    "ENTER\n"
                                    "STRING ipconfig /all\n"
                                    "ENTER\n"
                                    "# 22 lines\n";
static const char C_RICK_TXT[] = "REM classic\nGUI r\nSTRING youtu.be/dQw4\nENTER\n# 8 lines\n";
static const char C_BOOT_LOG[] = "[01] boot TentacleOS\n"
                                 "[01] heap 8231 KB\n"
                                 "[02] littlefs ok\n"
                                 "[03] c5 handshake ok\n"
                                 "[05] ui home loaded\n"
                                 "[30] batt 76% 4.02V\n"
                                 "# end of log\n";
static const char C_CRASH_TXT[] = "assert @ nfc.c:214\nheap: 142 KB free\ntask: nfc_poll\n";

static const file_node_t NFC_SAVED[] = {
    {"office_badge.nfc",
     FT_NFC,
     NULL,
     0,
     "Mifare 1K  UID 1A2B3C4D",
     "Block 0: 1A 2B 3C 4D 08 04",
     C_OFFICE_NFC},
    {"transit_card.nfc",
     FT_NFC,
     NULL,
     0,
     "MF Ultralight  04A3F29C",
     "Page 04: 12 34 56 78",
     C_TRANSIT_NFC},
};
static const file_node_t NFC_DUMPS[] = {
    {"mystery_tag.nfc",
     FT_NFC,
     NULL,
     0,
     "ISO14443-3  88045F2A",
     "ATQA 00 44   SAK 00",
     C_MYSTERY_NFC},
};
static const file_node_t NFC_KIDS[] = {
    {"saved", FT_DIR, NFC_SAVED, ARRLEN(NFC_SAVED), NULL, NULL, NULL},
    {"dumps", FT_DIR, NFC_DUMPS, ARRLEN(NFC_DUMPS), NULL, NULL, NULL},
};

static const file_node_t SUB_CAP[] = {
    {"gate_433.sub",
     FT_SUB,
     NULL,
     0,
     "433.92 MHz  Princeton",
     "Key: 00 00 00 1A 2B 3C",
     C_GATE_SUB},
    {"tpms_front.sub", FT_SUB, NULL, 0, "315 MHz  TPMS", "Pressure 32 psi  24C", C_TPMS_SUB},
};
static const file_node_t SUB_PLAY[] = {
    {"sweep.txt", FT_LOG, NULL, 0, "6 lines", "433.92  315.00  868.35", C_SWEEP_TXT},
};
static const file_node_t SUB_KIDS[] = {
    {"captures", FT_DIR, SUB_CAP, ARRLEN(SUB_CAP), NULL, NULL, NULL},
    {"playlists", FT_DIR, SUB_PLAY, ARRLEN(SUB_PLAY), NULL, NULL, NULL},
};

static const file_node_t IR_TV[] = {
    {"samsung.ir", FT_IR, NULL, 0, "NECext  4 cmds", "Power / Vol+ / Vol- / Mute", C_SAMSUNG_IR},
    {"lg.ir", FT_IR, NULL, 0, "NEC  6 cmds", "Power / CH+ / CH- / Menu", C_LG_IR},
};
static const file_node_t IR_AC[] = {
    {"midea.ir", FT_IR, NULL, 0, "Midea  kit", "Cool 24C   Fan auto", C_MIDEA_IR},
};
static const file_node_t IR_KIDS[] = {
    {"tv", FT_DIR, IR_TV, ARRLEN(IR_TV), NULL, NULL, NULL},
    {"ac", FT_DIR, IR_AC, ARRLEN(IR_AC), NULL, NULL, NULL},
};

static const file_node_t BADUSB_FILES[] = {
    {"payload_win.txt", FT_LOG, NULL, 0, "22 lines", "GUI r  DELAY 200  STRING cmd", C_PAYLOAD_TXT},
    {"rickroll.txt", FT_LOG, NULL, 0, "8 lines", "...never gonna give...", C_RICK_TXT},
};

static const file_node_t LOGS_FILES[] = {
    {"boot.log", FT_LOG, NULL, 0, "18 lines", "[00:00:01] boot: TentacleOS", C_BOOT_LOG},
    {"crash.txt", FT_LOG, NULL, 0, "3 lines", "assert @ nfc.c:214", C_CRASH_TXT},
};

static const file_node_t ROOT_KIDS[] = {
    {"nfc", FT_DIR, NFC_KIDS, ARRLEN(NFC_KIDS), NULL, NULL, NULL},
    {"subghz", FT_DIR, SUB_KIDS, ARRLEN(SUB_KIDS), NULL, NULL, NULL},
    {"infrared", FT_DIR, IR_KIDS, ARRLEN(IR_KIDS), NULL, NULL, NULL},
    {"badusb", FT_DIR, BADUSB_FILES, ARRLEN(BADUSB_FILES), NULL, NULL, NULL},
    {"logs", FT_DIR, LOGS_FILES, ARRLEN(LOGS_FILES), NULL, NULL, NULL},
};
static const file_node_t ROOT = {"Files", FT_DIR, ROOT_KIDS, ARRLEN(ROOT_KIDS), NULL, NULL, NULL};

#define MAX_ENTRIES 16
#define MAX_DEPTH   8

static const char *ICON_OF[] = {
    [FT_DIR] = "/assets/icons/folder_icon.bin",
    [FT_IR] = "/assets/icons/ir_icon.bin",
    [FT_SUB] = "/assets/icons/radar_icon.bin",
    [FT_NFC] = "/assets/icons/nfc_icon.bin",
    [FT_LOG] = "/assets/icons/file_icon.bin",
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

typedef enum { VIEW_LIST = 0, VIEW_GRID } view_t;

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_timer = NULL;
static view_t s_view = VIEW_LIST;

static uint8_t s_path[MAX_DEPTH];
static uint8_t s_sel_stack[MAX_DEPTH];
static int s_depth = 0;
static int s_sel = 0;
static bool s_in_viewer = false;

static lv_obj_t *s_item[MAX_ENTRIES];
static lv_obj_t *s_item_name[MAX_ENTRIES];
static int s_item_count = 0;
static lv_obj_t *s_inner = NULL;
static lv_obj_t *s_pk_icon, *s_pk_name, *s_pk_tag, *s_pk_meta, *s_pk_snip;
static lv_obj_t *s_gl_name, *s_gl_type;
static lv_obj_t *s_vbody = NULL;

static bool s_up_last, s_down_last, s_left_last, s_right_last, s_ok_last, s_back_last;
static uint32_t s_ok_down_since = 0;
static bool s_ok_long_fired = false;
static bool s_ok_armed = false;

static void nav_timer_cb(lv_timer_t *t);
static void build_screen(void);

static const file_node_t *cur_dir(void) {
  const file_node_t *n = &ROOT;
  for (int i = 0; i < s_depth; i++)
    n = &n->children[s_path[i]];
  return n;
}
static void dir_counts(const file_node_t *d, int *nd, int *nf) {
  int a = 0, b = 0;
  for (int i = 0; i < d->child_count; i++)
    (d->children[i].type == FT_DIR) ? a++ : b++;
  *nd = a;
  *nf = b;
}

static void style_item(lv_obj_t *o, uint8_t type, bool sel) {
  lv_color_t c = color_of(type);
  lv_obj_set_style_border_color(o, sel ? c : current_theme.border_inactive, 0);
  lv_obj_set_style_border_opa(o, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(o, sel ? lv_color_hex(COL_RAISE) : current_theme.bg_secondary, 0);
  lv_obj_set_style_shadow_width(o, sel ? 14 : 0, 0);
  lv_obj_set_style_shadow_color(o, c, 0);
  lv_obj_set_style_shadow_spread(o, sel ? -3 : 0, 0);
}

static void fill_peek(const file_node_t *e) {
  if (!s_pk_name)
    return;
  lv_color_t c = color_of(e->type);
  lv_image_dsc_t *ic = assets_get(ICON_OF[e->type]);
  if (ic && s_pk_icon)
    lv_image_set_src(s_pk_icon, ic);
  lv_label_set_text(s_pk_name, e->name);
  lv_label_set_text(s_pk_tag, tag_of(e->type));
  lv_obj_set_style_text_color(s_pk_tag, c, 0);
  lv_obj_set_style_border_color(s_pk_tag, c, 0);

  if (e->type == FT_DIR) {
    int nd, nf;
    dir_counts(e, &nd, &nf);
    lv_label_set_text_fmt(s_pk_meta, "%d folders   %d files", nd, nf);
    char buf[96];
    int off = 0;
    buf[0] = '\0';
    for (int i = 0; i < e->child_count && i < 4 && off < (int)sizeof(buf) - 1; i++)
      off += snprintf(buf + off, sizeof(buf) - off, "%s%s", i ? ", " : "", e->children[i].name);
    if (e->child_count == 0)
      snprintf(buf, sizeof(buf), "(empty)");
    lv_label_set_text(s_pk_snip, buf);
  } else {
    lv_label_set_text(s_pk_meta, e->meta ? e->meta : "");
    lv_label_set_text(s_pk_snip, e->snip ? e->snip : "");
  }
}

static void fill_glabel(const file_node_t *e) {
  if (!s_gl_name)
    return;
  lv_label_set_text(s_gl_name, e->name);
  if (e->type == FT_DIR) {
    int nd, nf;
    dir_counts(e, &nd, &nf);
    lv_label_set_text_fmt(s_gl_type, "%d items", nd + nf);
  } else {
    lv_label_set_text(s_gl_type, e->meta ? e->meta : tag_of(e->type));
  }
  lv_obj_set_style_text_color(s_gl_type, color_of(e->type), 0);
}

static void refresh_selection(void) {
  const file_node_t *d = cur_dir();
  if (d->child_count == 0)
    return;
  if (s_sel < 0)
    s_sel = 0;
  if (s_sel >= d->child_count)
    s_sel = d->child_count - 1;

  for (int i = 0; i < s_item_count; i++)
    style_item(s_item[i], d->children[i].type, i == s_sel);

  if (s_view == VIEW_LIST) {
    if (s_inner) {
      int top = s_sel - 1;
      int maxtop = d->child_count - LIST_VIS;
      if (maxtop < 0)
        maxtop = 0;
      if (top < 0)
        top = 0;
      if (top > maxtop)
        top = maxtop;
      lv_obj_set_style_translate_y(s_inner, -top * ROW_STEP, 0);
    }
    fill_peek(&d->children[s_sel]);
  } else {
    fill_glabel(&d->children[s_sel]);
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
  const file_node_t *d = cur_dir();
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
  lv_label_set_text(name, d->name);
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

  char p[96];
  int off = snprintf(p, sizeof(p), "root");
  const file_node_t *n = &ROOT;
  for (int i = 0; i < s_depth && off < (int)sizeof(p) - 1; i++) {
    n = &n->children[s_path[i]];
    off += snprintf(p + off, sizeof(p) - off, " / %s", n->name);
  }
  lv_obj_t *lbl = lv_label_create(bar);
  lv_label_set_text(lbl, p);
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

static lv_obj_t *type_icon(lv_obj_t *parent, uint8_t type, int cell) {
  lv_image_dsc_t *ic = assets_get(ICON_OF[type]);
  if (!ic)
    return NULL;
  lv_obj_t *img = lv_image_create(parent);
  lv_image_set_src(img, ic);
  lv_obj_set_size(img, cell, cell);
  lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
  return img;
}

static lv_obj_t *make_row(lv_obj_t *parent, const file_node_t *e, lv_obj_t **name_out) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, lv_pct(100), ROW_H);
  lv_obj_set_style_radius(row, 8, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_pad_all(row, 0, 0);

  lv_obj_t *ic = type_icon(row, e->type, 20);
  if (ic)
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 8, 0);

  lv_obj_t *name = lv_label_create(row);
  lv_obj_set_width(name, e->type == FT_DIR ? ROW_NAME_W_DIR : ROW_NAME_W_FILE);
  lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_label_set_text(name, e->name);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(
      name, e->type == FT_DIR ? lv_color_hex(COL_DIRNM) : current_theme.text_main, 0);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, ROW_NAME_X, 0);
  if (name_out)
    *name_out = name;

  if (e->type == FT_DIR) {
    int nd, nf;
    dir_counts(e, &nd, &nf);
    lv_obj_t *chev = lv_label_create(row);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(chev, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(chev, color_of(FT_DIR), 0);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_t *cnt = lv_label_create(row);
    lv_label_set_text_fmt(cnt, "%d", nd + nf);
    lv_obj_set_style_text_font(cnt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cnt, lv_color_hex(COL_DIM), 0);
    lv_obj_align(cnt, LV_ALIGN_RIGHT_MID, -24, 0);
  }
  return row;
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

  s_pk_icon = type_icon(hrow, FT_NFC, 16);

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

static void build_list(void) {
  const file_node_t *d = cur_dir();
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
  lv_obj_set_style_clip_corner(wrap, true, 0);

  s_inner = plain(wrap);
  lv_obj_set_width(s_inner, lv_pct(100));
  lv_obj_set_height(s_inner, LV_SIZE_CONTENT);
  lv_obj_align(s_inner, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(s_inner, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_inner, ROW_GAP, 0);

  s_item_count = 0;
  for (int i = 0; i < d->child_count && i < MAX_ENTRIES; i++) {
    s_item[i] = make_row(s_inner, &d->children[i], &s_item_name[i]);
    s_item_count++;
  }
}

static lv_obj_t *make_tile(lv_obj_t *parent, const file_node_t *e, lv_obj_t **name_out) {
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

  type_icon(tile, e->type, 28);

  lv_obj_t *nm = lv_label_create(tile);
  lv_obj_set_width(nm, TILE_W - 12);
  lv_label_set_long_mode(nm, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_label_set_text(nm, e->name);
  lv_obj_set_style_text_align(nm, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(
      nm, e->type == FT_DIR ? lv_color_hex(COL_DIRNM) : current_theme.text_main, 0);
  if (name_out)
    *name_out = nm;

  if (e->type == FT_DIR) {
    int nd, nf;
    dir_counts(e, &nd, &nf);
    lv_obj_t *badge = lv_label_create(tile);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_label_set_text_fmt(badge, "%d", nd + nf);
    lv_obj_set_style_text_font(badge, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(badge, color_of(FT_DIR), 0);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -2, 1);
  }
  return tile;
}

static void build_grid(void) {
  const file_node_t *d = cur_dir();
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

  s_item_count = 0;
  for (int i = 0; i < d->child_count && i < MAX_ENTRIES; i++) {
    s_item[i] = make_tile(g, &d->children[i], &s_item_name[i]);
    s_item_count++;
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

static void build_viewer_header(const file_node_t *e, int lines) {
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

  type_icon(hdr, e->type, 20);

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

static char s_vbuf[512];
static char s_gbuf[256];

static void build_viewer(void) {
  const file_node_t *e = &cur_dir()->children[s_sel];
  const char *content = e->content ? e->content : "";

  int n = 0;
  for (const char *p = content; *p && n < (int)sizeof(s_vbuf) - 1; p++)
    s_vbuf[n++] = *p;
  while (n > 0 && (s_vbuf[n - 1] == '\n' || s_vbuf[n - 1] == '\r'))
    n--;
  s_vbuf[n] = '\0';
  int lines = 1;
  for (int i = 0; i < n; i++)
    if (s_vbuf[i] == '\n')
      lines++;

  int g = 0;
  for (int i = 1; i <= lines && g < (int)sizeof(s_gbuf) - 16; i++)
    g += snprintf(s_gbuf + g, sizeof(s_gbuf) - g, "%d\n", i);
  if (g > 0 && s_gbuf[g - 1] == '\n')
    s_gbuf[g - 1] = '\0';

  build_viewer_header(e, lines);

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_set_size(body, LCD_H_RES, LCD_V_RES - HEADER_H - FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, HEADER_H);
  lv_obj_set_style_radius(body, 0, 0);
  lv_obj_set_style_bg_color(body, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 0, 0);
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
  lv_obj_set_width(gut, 22);
  lv_obj_set_style_text_align(gut, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(gut, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(gut, lv_color_hex(COL_DIM), 0);
  lv_obj_align(gut, LV_ALIGN_TOP_LEFT, 8, 8);

  lv_obj_t *txt = lv_label_create(body);
  lv_label_set_text(txt, s_vbuf);
  lv_obj_set_style_text_font(txt, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(txt, current_theme.text_main, 0);
  lv_obj_align(txt, LV_ALIGN_TOP_LEFT, 40, 8);

  lv_obj_scroll_to_y(body, 0, LV_ANIM_OFF);

  build_footer("UP/DOWN scroll    BACK close");
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_inner = NULL;
  s_vbody = NULL;
  s_pk_icon = s_pk_name = s_pk_tag = s_pk_meta = s_pk_snip = NULL;
  s_gl_name = s_gl_type = NULL;
  s_item_count = 0;

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

  if (s_timer == NULL)
    s_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void move_list(int d) {
  int n = cur_dir()->child_count;
  if (n <= 0)
    return;
  s_sel = (s_sel + d + n) % n;
  refresh_selection();
  ui_feedback(UI_FB_NAV);
}
static void move_grid(int dx, int dy) {
  int n = cur_dir()->child_count;
  if (n <= 0)
    return;
  int s = s_sel;
  if (dx > 0 && s < n - 1)
    s++;
  if (dx < 0 && s > 0)
    s--;
  if (dy > 0) {
    int t = s + 3;
    s = (t < n) ? t : n - 1;
  }
  if (dy < 0) {
    int t = s - 3;
    if (t >= 0)
      s = t;
  }
  if (s != s_sel) {
    s_sel = s;
    refresh_selection();
    ui_feedback(UI_FB_NAV);
  }
}
static void do_enter(void) {
  const file_node_t *e = &cur_dir()->children[s_sel];
  if (e->type == FT_DIR) {
    if (e->child_count == 0)
      return;
    s_sel_stack[s_depth] = (uint8_t)s_sel;
    s_path[s_depth] = (uint8_t)s_sel;
    s_depth++;
    s_sel = 0;
    ui_feedback(UI_FB_SELECT);
    build_screen();
  } else {
    s_in_viewer = true;
    ui_feedback(UI_FB_SELECT);
    build_screen();
  }
}
static void do_back(void) {
  if (s_in_viewer) {
    s_in_viewer = false;
    build_screen();
    return;
  }
  if (s_depth > 0) {
    s_depth--;
    s_sel = s_sel_stack[s_depth];
    ui_feedback(UI_FB_NAV);
    build_screen();
  } else {
    ui_switch_screen(SCREEN_MENU);
  }
}
static void toggle_view(void) {
  s_view = (s_view == VIEW_LIST) ? VIEW_GRID : VIEW_LIST;
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
  s_depth = 0;
  s_sel = 0;
  s_in_viewer = false;
  s_ok_long_fired = false;
  s_ok_armed = false;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;
  build_screen();
}
