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

#include "msgbox_ui.h"

#include "st7789.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define MSGBOX_H       ((ui_screen_h() * 45) / 100)
#define ANIM_TIME      300
#define BORDER_COLOR   current_theme.border_accent
#define GRAD_TOP       current_theme.border_interface
#define GRAD_BOT       current_theme.bg_secondary
#define BTN_W          80
#define BTN_H          28
#define MSGBOX_POLL_MS 50

#define SD_MODAL_W     200
#define SD_MODAL_H     200
#define SD_MODAL_BTN_W 150
#define SD_ICON_PX     34
#define SD_BTN_RADIUS  14

static lv_obj_t *panel = NULL;
static lv_obj_t *btn_objs[2] = {NULL};
static int btn_count = 0;
static int btn_sel = 0;
static bool btn_confirms[2] = {false, false};
static msgbox_cb_t current_cb = NULL;
static lv_timer_t *msgbox_timer = NULL;

static bool btn_left_last = false;
static bool btn_right_last = false;
static bool btn_ok_last = false;
static bool btn_back_last = false;
static bool input_locked = true;

static void update_btn_selection(void) {
  for (int i = 0; i < btn_count; i++) {
    if (!btn_objs[i])
      continue;
    if (i == btn_sel) {
      lv_obj_set_style_border_width(btn_objs[i], 2, 0);
      lv_obj_set_style_border_color(btn_objs[i], BORDER_COLOR, 0);
    } else {
      lv_obj_set_style_border_width(btn_objs[i], 1, 0);
      lv_obj_set_style_border_color(btn_objs[i], current_theme.border_interface, 0);
    }
  }
}

static void slide_anim_cb(void *var, int32_t val) {
  lv_obj_set_y((lv_obj_t *)var, val);
}

static void close_anim_del_cb(lv_anim_t *a) {
  lv_obj_del((lv_obj_t *)a->var);
}

static void panel_deleted_cb(lv_event_t *e) {
  (void)e;
  panel = NULL;
  btn_objs[0] = btn_objs[1] = NULL;
  btn_count = 0;
  current_cb = NULL;
  if (msgbox_timer) {
    lv_timer_delete(msgbox_timer);
    msgbox_timer = NULL;
  }
}

static void do_close(bool confirm) {
  if (!panel)
    return;

  lv_obj_t *closing = panel;
  msgbox_cb_t cb = current_cb;
  panel = NULL;
  current_cb = NULL;
  btn_objs[0] = btn_objs[1] = NULL;
  btn_count = 0;
  if (msgbox_timer) {
    lv_timer_delete(msgbox_timer);
    msgbox_timer = NULL;
  }
  lv_obj_remove_event_cb(closing, panel_deleted_cb);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, closing);
  lv_anim_set_values(&a, lv_obj_get_y(closing), ui_screen_h());
  lv_anim_set_duration(&a, ANIM_TIME);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, slide_anim_cb);
  lv_anim_set_completed_cb(&a, close_anim_del_cb);
  lv_anim_start(&a);

  if (cb)
    cb(confirm);
}

static void discard_panel_silent(void) {
  if (!panel)
    return;
  lv_obj_t *p = panel;
  panel = NULL;
  current_cb = NULL;
  btn_objs[0] = btn_objs[1] = NULL;
  btn_count = 0;
  if (msgbox_timer) {
    lv_timer_delete(msgbox_timer);
    msgbox_timer = NULL;
  }
  lv_obj_remove_event_cb(p, panel_deleted_cb);
  lv_anim_delete(p, NULL);
  lv_obj_del(p);
}

static void msgbox_timer_cb(lv_timer_t *t) {
  if (!panel || !lv_obj_is_valid(panel)) {
    lv_timer_delete(t);
    msgbox_timer = NULL;
    return;
  }

  bool left = left_button_is_down();
  bool right = right_button_is_down();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (input_locked) {
    if (!ok && !back && !left && !right)
      input_locked = false;
    btn_left_last = left;
    btn_right_last = right;
    btn_ok_last = ok;
    btn_back_last = back;
    return;
  }

  if (left && !btn_left_last && btn_count > 1) {
    btn_sel = (btn_sel == 0) ? btn_count - 1 : btn_sel - 1;
    update_btn_selection();
  }
  if (right && !btn_right_last && btn_count > 1) {
    btn_sel = (btn_sel + 1) % btn_count;
    update_btn_selection();
  }
  if (ok && !btn_ok_last && btn_count > 0) {
    do_close(btn_confirms[btn_sel]);
  }
  if (back && !btn_back_last) {
    do_close(false);
  }

  btn_left_last = left;
  btn_right_last = right;
  btn_ok_last = ok;
  btn_back_last = back;
}

static lv_obj_t *create_btn(lv_obj_t *parent, const char *text) {
  lv_obj_t *btn = lv_obj_create(parent);
  lv_obj_set_size(btn, BTN_W, BTN_H);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(btn, 10, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn, GRAD_BOT, 0);
  lv_obj_set_style_bg_grad_color(btn, GRAD_TOP, 0);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, current_theme.border_interface, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(lbl);

  return btn;
}

void msgbox_open(
    const char *icon, const char *msg, const char *btn_ok, const char *btn_cancel, msgbox_cb_t cb) {
  if (panel)
    discard_panel_silent();

  current_cb = cb;
  input_locked = true;
  btn_count = 0;
  btn_sel = 0;

  lv_obj_t *scr = lv_screen_active();
  panel = lv_obj_create(scr);
  lv_obj_set_size(panel, ui_screen_w(), MSGBOX_H);
  lv_obj_set_pos(panel, 0, ui_screen_h());
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(panel, panel_deleted_cb, LV_EVENT_DELETE, NULL);

  lv_obj_set_style_radius(panel, 12, 0);
  lv_obj_set_style_border_side(
      panel, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_width(panel, 2, 0);
  lv_obj_set_style_border_color(panel, BORDER_COLOR, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(panel, GRAD_BOT, 0);
  lv_obj_set_style_bg_grad_color(panel, GRAD_TOP, 0);
  lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);

  lv_obj_move_foreground(panel);

  lv_obj_t *content = lv_obj_create(panel);
  lv_obj_set_size(content, lv_pct(100), lv_pct(100));
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 10, 0);
  lv_obj_set_style_pad_row(content, 8, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  static lv_image_dsc_t *warn_dsc = NULL;
  if (!warn_dsc)
    warn_dsc = assets_get("/assets/icons/warning.bin");
  if (warn_dsc) {
    lv_obj_t *icon_img = lv_image_create(content);
    lv_image_set_src(icon_img, warn_dsc);
  }

  lv_obj_t *msg_lbl = lv_label_create(content);
  lv_label_set_text(msg_lbl, msg ? msg : "");
  lv_obj_set_style_text_color(msg_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg_lbl, ui_screen_w() - 40);

  lv_obj_t *btn_row = lv_obj_create(content);
  lv_obj_set_size(btn_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn_row, 0, 0);
  lv_obj_set_style_pad_all(btn_row, 0, 0);
  lv_obj_set_style_pad_column(btn_row, 15, 0);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  bool has_cancel = btn_cancel && btn_cancel[0];
  bool has_ok = btn_ok && btn_ok[0];

  if (has_cancel) {
    btn_objs[btn_count] = create_btn(btn_row, btn_cancel);
    btn_confirms[btn_count] = false;
    btn_count++;
  }
  if (has_ok) {
    btn_objs[btn_count] = create_btn(btn_row, btn_ok);
    btn_confirms[btn_count] = true;
    btn_sel = btn_count;
    btn_count++;
  }

  update_btn_selection();

  int target_y = ui_screen_h() - MSGBOX_H;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, panel);
  lv_anim_set_values(&a, ui_screen_h(), target_y);
  lv_anim_set_duration(&a, ANIM_TIME);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, slide_anim_cb);
  lv_anim_start(&a);

  if (!msgbox_timer)
    msgbox_timer = lv_timer_create(msgbox_timer_cb, MSGBOX_POLL_MS, NULL);
}

static void sd_add_info_row(lv_obj_t *parent, const char *k, const char *v) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *kl = lv_label_create(row);
  lv_label_set_text(kl, k);
  lv_obj_set_style_text_color(kl, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(kl, LV_OPA_60, 0);
  lv_obj_set_style_text_font(kl, &lv_font_montserrat_12, 0);

  lv_obj_t *vl = lv_label_create(row);
  lv_label_set_text(vl, v);
  lv_obj_set_style_text_color(vl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(vl, &lv_font_montserrat_12, 0);
}

void msgbox_open_sd_info(const char *name, const char *size, const char *free, const char *fmt) {
  if (panel)
    discard_panel_silent();

  current_cb = NULL;
  input_locked = true;
  btn_count = 0;
  btn_sel = 0;

  lv_obj_t *scr = lv_screen_active();
  panel = lv_obj_create(scr);
  lv_obj_set_size(panel, SD_MODAL_W, SD_MODAL_H);
  lv_obj_set_pos(panel, (ui_screen_w() - SD_MODAL_W) / 2, ui_screen_h());
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(panel, panel_deleted_cb, LV_EVENT_DELETE, NULL);

  lv_obj_set_style_radius(panel, 14, 0);
  lv_obj_set_style_border_width(panel, 2, 0);
  lv_obj_set_style_border_color(panel, BORDER_COLOR, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(panel, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(panel, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_pad_top(panel, 14, 0);
  lv_obj_set_style_pad_bottom(panel, 14, 0);
  lv_obj_set_style_pad_left(panel, 12, 0);
  lv_obj_set_style_pad_right(panel, 12, 0);
  lv_obj_set_style_pad_row(panel, 6, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_move_foreground(panel);

  static lv_image_dsc_t *card_dsc = NULL;
  if (!card_dsc)
    card_dsc = assets_get("/assets/icons/sd_card.bin");
  if (card_dsc) {
    lv_obj_t *ci = lv_image_create(panel);
    lv_image_set_src(ci, card_dsc);
    lv_obj_set_size(ci, SD_ICON_PX, SD_ICON_PX);
    lv_image_set_inner_align(ci, LV_IMAGE_ALIGN_CONTAIN);
  }

  lv_obj_t *title = lv_label_create(panel);
  lv_label_set_text(title, "SD Card Connected");
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

  if (name && name[0]) {
    lv_obj_t *sub = lv_label_create(panel);
    lv_label_set_text(sub, name);
    lv_obj_set_style_text_color(sub, current_theme.text_main, 0);
    lv_obj_set_style_text_opa(sub, LV_OPA_50, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  }

  lv_obj_t *info = lv_obj_create(panel);
  lv_obj_set_size(info, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(info, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(info, 0, 0);
  lv_obj_set_style_pad_all(info, 0, 0);
  lv_obj_set_style_pad_row(info, 3, 0);
  lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
  sd_add_info_row(info, "Capacity", size ? size : "-");
  sd_add_info_row(info, "Free", free ? free : "-");
  sd_add_info_row(info, "Format", fmt ? fmt : "-");

  lv_obj_t *btn = create_btn(panel, "OK");
  lv_obj_set_width(btn, SD_MODAL_BTN_W);
  lv_obj_set_style_radius(btn, SD_BTN_RADIUS, 0);
  btn_objs[0] = btn;
  btn_confirms[0] = true;
  btn_count = 1;
  btn_sel = 0;
  update_btn_selection();

  int target_y = (ui_screen_h() - SD_MODAL_H) / 2;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, panel);
  lv_anim_set_values(&a, ui_screen_h(), target_y);
  lv_anim_set_duration(&a, ANIM_TIME);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, slide_anim_cb);
  lv_anim_start(&a);

  if (!msgbox_timer)
    msgbox_timer = lv_timer_create(msgbox_timer_cb, MSGBOX_POLL_MS, NULL);
}

void msgbox_open_info(const char *icon_path,
                      const char *title,
                      const char *msg,
                      lv_color_t accent) {
  if (panel)
    discard_panel_silent();

  current_cb = NULL;
  input_locked = true;
  btn_count = 0;
  btn_sel = 0;

  lv_obj_t *scr = lv_screen_active();
  panel = lv_obj_create(scr);
  lv_obj_set_size(panel, SD_MODAL_W, SD_MODAL_H);
  lv_obj_set_pos(panel, (ui_screen_w() - SD_MODAL_W) / 2, ui_screen_h());
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(panel, panel_deleted_cb, LV_EVENT_DELETE, NULL);

  lv_obj_set_style_radius(panel, 14, 0);
  lv_obj_set_style_border_width(panel, 2, 0);
  lv_obj_set_style_border_color(panel, accent, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(panel, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(panel, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_shadow_width(panel, 26, 0);
  lv_obj_set_style_shadow_color(panel, accent, 0);
  lv_obj_set_style_shadow_opa(panel, LV_OPA_40, 0);
  lv_obj_set_style_pad_all(panel, 14, 0);
  lv_obj_set_style_pad_row(panel, 8, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_move_foreground(panel);

  if (icon_path) {
    lv_image_dsc_t *dsc = assets_get(icon_path);
    if (dsc) {
      lv_obj_t *badge = lv_obj_create(panel);
      lv_obj_set_size(badge, 48, 48);
      lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(badge, accent, 0);
      lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
      lv_obj_set_style_border_width(badge, 2, 0);
      lv_obj_set_style_border_color(badge, accent, 0);
      lv_obj_set_style_pad_all(badge, 0, 0);
      lv_obj_t *ci = lv_image_create(badge);
      lv_image_set_src(ci, dsc);
      lv_obj_set_size(ci, SD_ICON_PX, SD_ICON_PX);
      lv_image_set_inner_align(ci, LV_IMAGE_ALIGN_CONTAIN);
      lv_obj_center(ci);
    }
  }

  lv_obj_t *title_lbl = lv_label_create(panel);
  lv_label_set_text(title_lbl, title ? title : "");
  lv_obj_set_style_text_color(title_lbl, accent, 0);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);

  lv_obj_t *msg_lbl = lv_label_create(panel);
  lv_label_set_text(msg_lbl, msg ? msg : "");
  lv_obj_set_style_text_color(msg_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg_lbl, SD_MODAL_W - 32);

  lv_obj_t *btn = create_btn(panel, "OK");
  lv_obj_set_width(btn, SD_MODAL_BTN_W);
  lv_obj_set_style_radius(btn, SD_BTN_RADIUS, 0);
  btn_objs[0] = btn;
  btn_confirms[0] = true;
  btn_count = 1;
  btn_sel = 0;
  update_btn_selection();

  int target_y = (ui_screen_h() - SD_MODAL_H) / 2;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, panel);
  lv_anim_set_values(&a, ui_screen_h(), target_y);
  lv_anim_set_duration(&a, ANIM_TIME);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, slide_anim_cb);
  lv_anim_start(&a);

  if (!msgbox_timer)
    msgbox_timer = lv_timer_create(msgbox_timer_cb, MSGBOX_POLL_MS, NULL);
}

void msgbox_close(void) {
  if (panel) {
    do_close(false);
  }
}

bool msgbox_is_open(void) {
  return (panel != NULL);
}
