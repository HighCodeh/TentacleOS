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

#include "card_emu_ui.h"

#include <string.h>

#include "lvgl.h"

#include "buttons_gpio.h"
#include "nfc_sim.h"
#include "nfc_ui_common.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 33
#define FIELD_GREEN  0x00E676
#define SCALE_FWD    285

enum { CE_BROWSE, CE_EDIT, CE_EMULATE };

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_panel = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_hint = NULL;
static lv_timer_t *s_timer = NULL;

static lv_obj_t *s_ov = NULL;
static lv_obj_t *s_field_box = NULL;
static nfc_ui_field_t s_field;
static bool s_field_ready = false;
static char s_emu_name[NFC_SIM_NAME_LEN];
static lv_draw_buf_t *s_snap = NULL;
static lv_obj_t *s_card_img = NULL;
static int s_card_top_y = 0;
static lv_obj_t *s_glow = NULL;

static int s_state = CE_BROWSE;
static int s_idx = 0;
static int s_edit_type = 0;
static nfc_sim_card_t s_edit;

static bool s_up_last, s_down_last, s_ok_last, s_back_last, s_left_last, s_right_last;

static lv_obj_t *make_new_panel(lv_obj_t *parent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, 210, 122);
  lv_obj_set_style_radius(p, 14, 0);
  lv_obj_set_style_bg_color(p, lv_color_hex(0x140828), 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(p, 2, 0);
  lv_obj_set_style_border_color(p, ui_theme_get_accent(), 0);
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, LV_SYMBOL_PLUS "  New Card");
  lv_obj_set_style_text_color(l, ui_theme_get_accent(), 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_center(l);
  return p;
}

static void rebuild_panel(void) {
  if (s_panel) {
    lv_obj_del(s_panel);
    s_panel = NULL;
  }
  int saved = nfc_sim_saved_count();
  if (s_state == CE_EDIT)
    s_panel = nfc_ui_card_panel(s_screen, &s_edit);
  else if (s_idx < saved)
    s_panel = nfc_ui_card_panel(s_screen, nfc_sim_saved_get(s_idx));
  else
    s_panel = make_new_panel(s_screen);
  lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 8);
}

static void refresh_text(void) {
  int saved = nfc_sim_saved_count();
  if (s_state == CE_EDIT) {
    lv_label_set_text_fmt(
        s_status, "New card  (type %d/%d)", s_edit_type + 1, nfc_sim_template_count());
    ui_chrome_footer_set_text(
        s_hint, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " type  " LV_SYMBOL_UP " UID   OK Save+Emu   BACK");
  } else if (s_idx < saved) {
    lv_label_set_text_fmt(s_status, "Card %d / %d", s_idx + 1, saved);
    ui_chrome_footer_set_text(s_hint, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " flip   OK Emulate   BACK");
  } else {
    lv_label_set_text(s_status, "Create a card");
    ui_chrome_footer_set_text(s_hint, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " flip   OK Create   BACK");
  }
}

static void anim_img_y_cb(void *var, int32_t v) {
  lv_obj_set_y((lv_obj_t *)var, v);
}
static void anim_img_scale_cb(void *var, int32_t v) {
  lv_image_set_scale((lv_obj_t *)var, (uint32_t)v);
}

static void anim_flip_x_cb(void *var, int32_t v) {
  lv_obj_align((lv_obj_t *)var, LV_ALIGN_CENTER, v, 8);
}
static void anim_flip_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}
static void flip_done(lv_anim_t *a) {
  (void)a;
  if (s_panel) {
    lv_obj_set_style_shadow_width(s_panel, 24, 0);
    lv_obj_set_style_opa(s_panel, LV_OPA_COVER, 0);
  }
}

static void do_flip(int new_idx, int dir) {
  s_idx = new_idx;
  rebuild_panel();
  refresh_text();
  lv_obj_set_style_shadow_width(s_panel, 0, 0);
  lv_obj_set_style_opa(s_panel, LV_OPA_TRANSP, 0);
  lv_obj_align(s_panel, LV_ALIGN_CENTER, dir * 60, 8);

  lv_anim_t ax;
  lv_anim_init(&ax);
  lv_anim_set_var(&ax, s_panel);
  lv_anim_set_values(&ax, dir * 60, 0);
  lv_anim_set_duration(&ax, 200);
  lv_anim_set_path_cb(&ax, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&ax, anim_flip_x_cb);
  lv_anim_set_completed_cb(&ax, flip_done);
  lv_anim_start(&ax);

  lv_anim_t ao;
  lv_anim_init(&ao);
  lv_anim_set_var(&ao, s_panel);
  lv_anim_set_values(&ao, 0, 255);
  lv_anim_set_duration(&ao, 200);
  lv_anim_set_exec_cb(&ao, anim_flip_opa_cb);
  lv_anim_start(&ao);
}
static void anim_card_settled(lv_anim_t *a) {
  (void)a;
  s_field_ready = true;
  if (s_field_box)
    lv_obj_fade_in(s_field_box, 300, 0);
  if (s_glow)
    lv_obj_fade_in(s_glow, 360, 0);
  if (s_card_img == NULL)
    return;

  lv_anim_t bob;
  lv_anim_init(&bob);
  lv_anim_set_var(&bob, s_card_img);
  lv_anim_set_values(&bob, s_card_top_y, s_card_top_y - 8);
  lv_anim_set_duration(&bob, 1600);
  lv_anim_set_playback_duration(&bob, 1600);
  lv_anim_set_repeat_count(&bob, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&bob, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&bob, anim_img_y_cb);
  lv_anim_start(&bob);

  lv_anim_t br;
  lv_anim_init(&br);
  lv_anim_set_var(&br, s_card_img);
  lv_anim_set_values(&br, SCALE_FWD, SCALE_FWD + 14);
  lv_anim_set_duration(&br, 1600);
  lv_anim_set_playback_duration(&br, 1600);
  lv_anim_set_repeat_count(&br, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&br, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&br, anim_img_scale_cb);
  lv_anim_start(&br);
}

static void emulate_close(void) {
  s_field_ready = false;
  if (s_field_box) {
    lv_obj_del(s_field_box);
    s_field_box = NULL;
  }
  if (s_ov) {
    lv_obj_del(s_ov);
    s_ov = NULL;
  }
  if (s_card_img) {
    lv_obj_del(s_card_img);
    s_card_img = NULL;
  }
  if (s_glow) {
    lv_obj_del(s_glow);
    s_glow = NULL;
  }
  if (s_snap) {
    lv_draw_buf_destroy(s_snap);
    s_snap = NULL;
  }
  for (int i = 0; i < 3; i++)
    s_field.ring[i] = NULL;
  if (s_status)
    lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  if (s_hint)
    lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
  s_state = CE_BROWSE;
  rebuild_panel();
  refresh_text();
}

static void emulate_start(const nfc_sim_card_t *card) {
  strncpy(s_emu_name, card->name, sizeof(s_emu_name) - 1);
  s_emu_name[sizeof(s_emu_name) - 1] = '\0';
  lv_color_t col = nfc_ui_card_color(card);

  int H = lv_display_get_vertical_resolution(NULL);
  if (H < 200)
    H = 320;
  int top_y = H * 8 / 100;
  if (top_y < 4)
    top_y = 4;
  s_card_top_y = top_y;

  s_ov = lv_obj_create(s_screen);
  lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
  lv_obj_center(s_ov);
  lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_ov, 0, 0);
  lv_obj_set_style_radius(s_ov, 0, 0);
  lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
  lv_obj_fade_in(s_ov, 220, 0);

  if (s_status)
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  if (s_hint)
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

  s_glow = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_glow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_glow, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_glow, 226, 152);
  lv_obj_align(s_glow, LV_ALIGN_CENTER, 0, top_y + 61 - H / 2);
  lv_obj_set_style_radius(s_glow, 38, 0);
  lv_obj_set_style_border_width(s_glow, 0, 0);
  lv_obj_set_style_bg_color(s_glow, col, 0);
  lv_obj_set_style_bg_opa(s_glow, LV_OPA_30, 0);
  lv_obj_set_style_opa(s_glow, LV_OPA_TRANSP, 0);

  s_field_box = lv_obj_create(s_screen);
  lv_obj_set_size(s_field_box, lv_pct(100), H * 44 / 100);
  lv_obj_align(s_field_box, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_remove_flag(s_field_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_field_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_field_box, 0, 0);
  lv_obj_set_style_opa(s_field_box, LV_OPA_TRANSP, 0);

  lv_obj_t *prompt = lv_label_create(s_field_box);
  lv_label_set_text(prompt, "Hold Near Reader");
  lv_obj_set_style_text_color(prompt, current_theme.text_main, 0);
  lv_obj_align(prompt, LV_ALIGN_TOP_MID, 0, 2);

  nfc_ui_field_create(&s_field, s_field_box, col);

  lv_obj_t *hint = lv_label_create(s_field_box);
  lv_label_set_text(hint, "BACK to stop");
  lv_obj_set_style_text_color(hint, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(hint, LV_OPA_60, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);

  lv_obj_set_style_shadow_width(s_panel, 0, 0);
  lv_obj_update_layout(s_screen);
  int cx = lv_obj_get_x(s_panel);
  int cy = lv_obj_get_y(s_panel);
  s_snap = lv_snapshot_take(s_panel, LV_COLOR_FORMAT_ARGB8888);

  s_field_ready = false;
  if (s_snap != NULL) {
    s_card_img = lv_image_create(s_screen);
    lv_image_set_src(s_card_img, s_snap);
    lv_obj_set_pos(s_card_img, cx, cy);
    lv_image_set_pivot(s_card_img, 105, 61);
    lv_image_set_scale(s_card_img, 256);
    lv_obj_set_style_opa(s_card_img, LV_OPA_COVER, 0);
    lv_obj_move_foreground(s_card_img);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t ay;
    lv_anim_init(&ay);
    lv_anim_set_var(&ay, s_card_img);
    lv_anim_set_values(&ay, cy, top_y);
    lv_anim_set_duration(&ay, 640);
    lv_anim_set_path_cb(&ay, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&ay, anim_img_y_cb);
    lv_anim_set_completed_cb(&ay, anim_card_settled);
    lv_anim_start(&ay);

    lv_anim_t as;
    lv_anim_init(&as);
    lv_anim_set_var(&as, s_card_img);
    lv_anim_set_values(&as, 256, SCALE_FWD);
    lv_anim_set_duration(&as, 560);
    lv_anim_set_path_cb(&as, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&as, anim_img_scale_cb);
    lv_anim_start(&as);
  } else {
    lv_obj_set_align(s_panel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_panel, cx, top_y);
    lv_obj_set_style_transform_pivot_x(s_panel, 105, 0);
    lv_obj_set_style_transform_pivot_y(s_panel, 61, 0);
    lv_obj_set_style_shadow_width(s_panel, 0, 0);
    lv_obj_set_style_transform_scale_x(s_panel, SCALE_FWD, 0);
    lv_obj_set_style_transform_scale_y(s_panel, SCALE_FWD, 0);
    lv_obj_move_foreground(s_panel);
    s_field_ready = true;
    lv_obj_fade_in(s_field_box, 300, 0);
    if (s_glow)
      lv_obj_fade_in(s_glow, 360, 0);
  }

  s_state = CE_EMULATE;
}

static void enter_edit(void) {
  s_state = CE_EDIT;
  s_edit_type = 0;
  nfc_sim_make_card(s_edit_type, &s_edit);
  rebuild_panel();
  refresh_text();
}

static void tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }
  bool up = ui_btn_up(), down = ui_btn_down();
  bool left = ui_btn_left(), right = ui_btn_right();
  bool ok = ok_button_is_down(), back = back_button_is_down();

  if (s_state == CE_EMULATE) {
    if (s_field_ready)
      nfc_ui_field_tick(&s_field, lv_tick_get());
    if (back && !s_back_last)
      emulate_close();
    goto save_edges;
  }

  if (ui_input_is_locked())
    goto save_edges;

  if (back && !s_back_last) {
    if (s_state == CE_EDIT) {
      s_state = CE_BROWSE;
      rebuild_panel();
      refresh_text();
    } else {
      ui_switch_screen(SCREEN_NFC_MENU);
      return;
    }
    goto save_edges;
  }

  if (s_state == CE_BROWSE) {
    int slots = nfc_sim_saved_count() + 1;
    if (right && !s_right_last)
      do_flip((s_idx + 1) % slots, +1);
    if (left && !s_left_last)
      do_flip((s_idx - 1 + slots) % slots, -1);
    if (ok && !s_ok_last) {
      int saved = nfc_sim_saved_count();
      if (s_idx < saved)
        emulate_start(nfc_sim_saved_get(s_idx));
      else
        enter_edit();
    }
  } else {
    int nt = nfc_sim_template_count();
    if (right && !s_right_last) {
      s_edit_type = (s_edit_type + 1) % nt;
      nfc_sim_make_card(s_edit_type, &s_edit);
      rebuild_panel();
      refresh_text();
    }
    if (left && !s_left_last) {
      s_edit_type = (s_edit_type - 1 + nt) % nt;
      nfc_sim_make_card(s_edit_type, &s_edit);
      rebuild_panel();
      refresh_text();
    }
    if (up && !s_up_last) {
      nfc_sim_make_card(s_edit_type, &s_edit);
      rebuild_panel();
    }
    if (ok && !s_ok_last) {
      if (nfc_sim_add(&s_edit))
        nfc_ui_play_sound(NFC_SND_SAVE);
      s_idx = nfc_sim_saved_count() - 1;
      if (s_idx < 0)
        s_idx = 0;
      emulate_start(&s_edit);
    }
  }

save_edges:
  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_card_emu_open(void) {
  nfc_sim_init();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_panel = NULL;
  s_ov = NULL;
  s_field_box = NULL;
  s_card_img = NULL;
  s_glow = NULL;
  s_snap = NULL;
  s_field_ready = false;
  for (int i = 0; i < 3; i++)
    s_field.ring[i] = NULL;
  s_state = CE_BROWSE;
  s_idx = 0;
  s_up_last = s_down_last = s_ok_last = s_back_last = s_left_last = s_right_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "CARD EMU", "/assets/icons/emulate_icon.bin");

  s_status = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 48);

  s_hint = ui_chrome_footer(s_screen, "");

  rebuild_panel();
  refresh_text();

  if (s_timer == NULL)
    s_timer = lv_timer_create(tick_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}
