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

#include "text_viewer_ui.h"

#include <stdio.h>
#include <stdlib.h>

#include "st7789.h"

#include "ui_chrome.h"
#include "ui_theme.h"

#define META_H      16 // "N lines · M bytes" line under the header
#define GAP         4  // vertical breathing room
#define BODY_PAD_H  10 // horizontal inset of the text
#define BODY_PAD_V  6  // vertical inset of the text
#define SCROLLBAR_W 4  // themed scrollbar width
#define LINE_SPACE  4  // extra spacing between wrapped lines

text_viewer_t text_viewer_create(lv_obj_t *parent, const char *filename) {
  text_viewer_t tv = {0};

  // Borderless full-screen surface; the chrome header/footer carry the framing so
  // the viewer matches every other screen.
  tv.screen = lv_obj_create(parent);
  lv_obj_set_size(tv.screen, LCD_H_RES, LCD_V_RES);
  lv_obj_align(tv.screen, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_remove_flag(tv.screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(tv.screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(tv.screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(tv.screen, 0, 0);
  lv_obj_set_style_border_width(tv.screen, 0, 0);
  lv_obj_set_style_radius(tv.screen, 0, 0);

  ui_chrome_header(tv.screen, filename ? filename : "File", NULL);

  // Meta line (line/byte count), right-aligned just under the header.
  tv.line_label = lv_label_create(tv.screen);
  lv_label_set_text(tv.line_label, "");
  lv_obj_set_style_text_color(tv.line_label, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(tv.line_label, LV_OPA_50, 0);
  lv_obj_set_style_text_font(tv.line_label, &lv_font_montserrat_12, 0);
  lv_obj_align(tv.line_label, LV_ALIGN_TOP_RIGHT, -BODY_PAD_H, UI_CHROME_HEADER_H + GAP);

  int content_y = UI_CHROME_HEADER_H + GAP + META_H + GAP;
  int content_h = LCD_V_RES - content_y - UI_CHROME_FOOTER_H - GAP;

  // Scrollable body: borderless, themed thin scrollbar, and NO elastic/momentum
  // so a button scroll clamps hard at the content bounds (no runaway scrolling).
  tv.text_area = lv_obj_create(tv.screen);
  lv_obj_set_size(tv.text_area, LCD_H_RES, content_h);
  lv_obj_align(tv.text_area, LV_ALIGN_TOP_LEFT, 0, content_y);
  lv_obj_set_style_bg_opa(tv.text_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tv.text_area, 0, 0);
  lv_obj_set_style_radius(tv.text_area, 0, 0);
  lv_obj_set_style_pad_hor(tv.text_area, BODY_PAD_H, 0);
  lv_obj_set_style_pad_ver(tv.text_area, BODY_PAD_V, 0);
  lv_obj_set_scroll_dir(tv.text_area, LV_DIR_VER);
  lv_obj_remove_flag(tv.text_area, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(tv.text_area, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_scrollbar_mode(tv.text_area, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(tv.text_area, current_theme.border_accent, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(tv.text_area, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(tv.text_area, SCROLLBAR_W, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(tv.text_area, 2, LV_PART_SCROLLBAR);

  ui_chrome_footer(tv.screen, LV_SYMBOL_UP LV_SYMBOL_DOWN "  Scroll      BACK  Close");

  return tv;
}

void text_viewer_load_file(text_viewer_t *tv, const char *path) {
  if (!tv || !tv->text_area || !path)
    return;

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    text_viewer_set_text(tv, "Failed to open file");
    return;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size < 0)
    size = 0;
  if (size > 4096)
    size = 4096;

  char *buf = malloc(size + 1);
  if (buf == NULL) {
    fclose(f);
    return;
  }

  size_t rd = fread(buf, 1, size, f);
  buf[rd] = '\0';
  fclose(f);

  text_viewer_set_text(tv, buf);
  free(buf);
}

void text_viewer_set_text(text_viewer_t *tv, const char *text) {
  if (!tv || !tv->text_area)
    return;

  lv_obj_clean(tv->text_area);

  lv_obj_t *lbl = lv_label_create(tv->text_area);
  lv_label_set_text(lbl, text ? text : "");
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_width(lbl, LCD_H_RES - BODY_PAD_H * 2 - SCROLLBAR_W - 2);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(lbl, LINE_SPACE, 0);

  if (tv->line_label && text) {
    int lines = 1;
    int len = 0;
    for (const char *p = text; *p; p++) {
      if (*p == '\n')
        lines++;
      len++;
    }
    if (len < 1024) {
      lv_label_set_text_fmt(tv->line_label, "%d lines  |  %d bytes", lines, len);
    } else {
      lv_label_set_text_fmt(tv->line_label, "%d lines  |  %.1f KB", lines, len / 1024.0f);
    }
  }

  lv_obj_scroll_to_y(tv->text_area, 0, LV_ANIM_OFF);
}
