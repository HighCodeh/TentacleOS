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

#include "capture_result_ui.h"

#include "st7789.h"

#include "assets_manager.h"
#include "ui_chrome.h"
#include "ui_theme.h"

#define COL_DIM   0x8A8594
#define COL_RAISE 0x170A28

#define CR_TOP    UI_CHROME_HEADER_H
#define CR_H      (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)
#define CARD_H    70
#define ROW_H     34
#define GAP       6
#define ICON_CELL 44

static const char *ACTION_SYM[CAP_ACT_MAX] = {
    [CAP_ACT_PRIMARY] = LV_SYMBOL_UPLOAD,
    [CAP_ACT_SAVE] = LV_SYMBOL_SAVE,
    [CAP_ACT_AGAIN] = LV_SYMBOL_REFRESH,
    [CAP_ACT_DISCARD] = LV_SYMBOL_CLOSE,
};

static lv_obj_t *bare(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  return o;
}

static void style_row(capture_result_t *cr, int i, bool sel) {
  lv_obj_set_style_border_color(cr->rows[i], sel ? cr->accent : current_theme.border_inactive, 0);
  lv_obj_set_style_border_opa(cr->rows[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(
      cr->rows[i], sel ? lv_color_hex(COL_RAISE) : current_theme.bg_secondary, 0);
  lv_obj_set_style_shadow_width(cr->rows[i], sel ? 14 : 0, 0);
  lv_obj_set_style_shadow_color(cr->rows[i], cr->accent, 0);
  lv_obj_set_style_shadow_spread(cr->rows[i], sel ? -3 : 0, 0);
  if (!(cr->saved && i == CAP_ACT_SAVE))
    lv_obj_set_style_text_color(cr->icons[i], sel ? cr->accent : lv_color_hex(COL_DIM), 0);
}

static void refresh(capture_result_t *cr) {
  for (int i = 0; i < cr->count; i++)
    style_row(cr, i, i == cr->sel);
}

static void make_card(capture_result_t *cr, lv_obj_t *root, const capture_result_cfg_t *cfg) {
  lv_obj_t *card = lv_obj_create(root);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, lv_pct(100), CARD_H);
  lv_obj_set_style_radius(card, 13, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, cr->accent, 0);
  lv_obj_set_style_shadow_width(card, 20, 0);
  lv_obj_set_style_shadow_color(card, cr->accent, 0);
  lv_obj_set_style_shadow_spread(card, -12, 0);
  lv_obj_set_style_pad_hor(card, 10, 0);
  lv_obj_set_style_pad_ver(card, 6, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(card, 11, 0);

  lv_obj_t *sq = lv_obj_create(card);
  lv_obj_remove_flag(sq, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(sq, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(sq, ICON_CELL, ICON_CELL);
  lv_obj_set_style_radius(sq, 11, 0);
  lv_obj_set_style_bg_color(sq, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(sq, 1, 0);
  lv_obj_set_style_border_color(sq, cr->accent, 0);
  lv_obj_set_style_pad_all(sq, 0, 0);
  lv_obj_set_style_clip_corner(sq, true, 0);
  if (cfg->card_icon) {
    lv_image_dsc_t *dsc = assets_get(cfg->card_icon);
    if (dsc) {
      lv_obj_t *img = lv_image_create(sq);
      lv_image_set_src(img, dsc);
      lv_obj_set_size(img, ICON_CELL - 12, ICON_CELL - 12);
      lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
      lv_obj_center(img);
    }
  }

  lv_obj_t *col = bare(card);
  lv_obj_set_size(col, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(col, 2, 0);

  lv_obj_t *title = lv_label_create(col);
  lv_obj_set_width(title, lv_pct(100));
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_label_set_text(title, cfg->card_title ? cfg->card_title : "");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, current_theme.text_main, 0);

  if (cfg->card_sub) {
    lv_obj_t *sub = lv_label_create(col);
    lv_obj_set_width(sub, lv_pct(100));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
    lv_label_set_text(sub, cfg->card_sub);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(COL_DIM), 0);
  }
  if (cfg->card_value) {
    lv_obj_t *val = lv_label_create(col);
    lv_obj_set_width(val, lv_pct(100));
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    lv_label_set_text(val, cfg->card_value);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(val, cr->accent, 0);
  }
}

static void make_action(capture_result_t *cr, lv_obj_t *root, int i, const char *label) {
  lv_obj_t *row = lv_obj_create(root);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, lv_pct(100), ROW_H);
  lv_obj_set_style_radius(row, 10, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_pad_hor(row, 12, 0);
  lv_obj_set_style_pad_ver(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 11, 0);

  lv_obj_t *ic = lv_label_create(row);
  lv_label_set_text(ic, ACTION_SYM[i]);
  lv_obj_set_width(ic, 18);
  lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_14, 0);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_flex_grow(lbl, 1);

  cr->rows[i] = row;
  cr->icons[i] = ic;
  cr->labels[i] = lbl;
}

capture_result_t capture_result_create(lv_obj_t *parent, const capture_result_cfg_t *cfg) {
  capture_result_t cr = {0};
  cr.accent = cfg->accent;
  cr.count = CAP_ACT_MAX;
  cr.sel = CAP_ACT_PRIMARY;
  cr.saved = false;

  lv_obj_t *root = bare(parent);
  lv_obj_set_size(root, LCD_H_RES, CR_H);
  lv_obj_align(root, LV_ALIGN_TOP_LEFT, 0, CR_TOP);
  lv_obj_set_style_pad_hor(root, 10, 0);
  lv_obj_set_style_pad_ver(root, 8, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(root, GAP, 0);
  cr.root = root;

  make_card(&cr, root, cfg);

  make_action(&cr, root, CAP_ACT_PRIMARY, cfg->primary_label ? cfg->primary_label : "Send");
  make_action(&cr, root, CAP_ACT_SAVE, "Save to library");
  make_action(&cr, root, CAP_ACT_AGAIN, cfg->again_label ? cfg->again_label : "Capture again");
  make_action(&cr, root, CAP_ACT_DISCARD, "Discard");

  refresh(&cr);
  return cr;
}

void capture_result_next(capture_result_t *cr) {
  if (!cr->root || cr->count <= 0)
    return;
  cr->sel = (cr->sel + 1) % cr->count;
  refresh(cr);
}

void capture_result_prev(capture_result_t *cr) {
  if (!cr->root || cr->count <= 0)
    return;
  cr->sel = (cr->sel - 1 + cr->count) % cr->count;
  refresh(cr);
}

capture_action_t capture_result_selected(const capture_result_t *cr) {
  return (capture_action_t)cr->sel;
}

void capture_result_mark_saved(capture_result_t *cr) {
  if (!cr->root || cr->saved)
    return;
  cr->saved = true;
  lv_label_set_text(cr->icons[CAP_ACT_SAVE], LV_SYMBOL_OK);
  lv_obj_set_style_text_color(cr->icons[CAP_ACT_SAVE], lv_color_hex(0x00E676), 0);
  lv_label_set_text(cr->labels[CAP_ACT_SAVE], "Saved");
}

void capture_result_destroy(capture_result_t *cr) {
  if (cr->root) {
    lv_obj_del(cr->root);
    cr->root = NULL;
  }
}
