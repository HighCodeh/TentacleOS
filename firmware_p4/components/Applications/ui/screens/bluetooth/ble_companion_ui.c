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

#include "ble_companion_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

#define PAIR_PHASE_MS         3000
#define COMPANION_ICON        "/assets/icons/app_shortcut.bin"

#define SIG_GREEN 0x00E676
#define COL_DIM   0x8A8594

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_body = NULL;
static lv_obj_t *s_footer = NULL;
static lv_timer_t *s_phase_timer = NULL;

static void companion_input(const input_event_t *ev, void *ctx);
static void phase_timer_cb(lv_timer_t *timer);
static void show_success(void);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h, int radius, int glow) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, radius, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(p, glow, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, -4, 0);
  return p;
}

static void pop_size_cb(void *var, int32_t v) {
  lv_obj_set_size((lv_obj_t *)var, v, v);
  lv_obj_center((lv_obj_t *)var);
}
static void pop_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}
static void pop_in(lv_obj_t *obj, int target_px, uint32_t ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
  lv_anim_set_exec_cb(&a, pop_size_cb);
  lv_anim_set_values(&a, 0, target_px);
  lv_anim_start(&a);

  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_set_exec_cb(&a, pop_opa_cb);
  lv_anim_set_values(&a, 0, LV_OPA_COVER);
  lv_anim_start(&a);
}

static lv_obj_t *make_body(lv_obj_t *parent) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(b, LCD_H_RES, LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(b, LV_ALIGN_TOP_LEFT, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  return b;
}

static void build_pairing(void) {
  waves_create(s_body, LV_ALIGN_CENTER, 0, -34, LV_SYMBOL_BLUETOOTH, NULL);

  lv_obj_t *status = lv_label_create(s_body);
  lv_label_set_text(status, "Waiting for app...");
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, 58);

  lv_obj_t *card = lit_panel(s_body, 150, 40, 13, 16);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 104);

  lv_obj_t *code = lv_label_create(card);
  lv_label_set_text(code, "CODE: 4821");
  lv_obj_set_style_text_color(code, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(code, &lv_font_montserrat_14, 0);
  lv_obj_center(code);

  fade_in(status, 200);
  fade_in(card, 240);
}

static void show_success(void) {
  lv_obj_clean(s_body);

  waves_create(s_body, LV_ALIGN_CENTER, 0, -34, LV_SYMBOL_OK, NULL);

  lv_obj_t *seal_slot = lv_obj_create(s_body);
  lv_obj_remove_flag(seal_slot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(seal_slot, 36, 36);
  lv_obj_set_style_bg_opa(seal_slot, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(seal_slot, 0, 0);
  lv_obj_set_style_pad_all(seal_slot, 0, 0);
  lv_obj_align(seal_slot, LV_ALIGN_CENTER, 0, -34);

  lv_obj_t *seal = lv_obj_create(seal_slot);
  lv_obj_remove_flag(seal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(seal, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(seal, lv_color_hex(SIG_GREEN), 0);
  lv_obj_set_style_bg_opa(seal, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(seal, 0, 0);
  lv_obj_set_style_pad_all(seal, 0, 0);
  lv_obj_set_size(seal, 0, 0);
  lv_obj_center(seal);

  lv_obj_t *seal_glyph = lv_label_create(seal);
  lv_label_set_text(seal_glyph, LV_SYMBOL_OK);
  lv_obj_set_style_text_color(seal_glyph, current_theme.text_main, 0);
  lv_obj_set_style_text_font(seal_glyph, &lv_font_montserrat_14, 0);
  lv_obj_center(seal_glyph);

  lv_obj_t *status = lv_label_create(s_body);
  lv_label_set_text(status, "Companion linked!");
  lv_obj_set_style_text_color(status, lv_color_hex(SIG_GREEN), 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, 42);

  lv_obj_t *badge = lit_panel(s_body, 200, 66, 14, 18);
  lv_obj_align(badge, LV_ALIGN_CENTER, 0, 92);
  lv_obj_set_style_pad_all(badge, 10, 0);
  lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(badge, 4, 0);

  lv_obj_t *name = lv_label_create(badge);
  lv_label_set_text(name, "HighBoy-Companion");
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);

  lv_obj_t *ver = lv_label_create(badge);
  lv_label_set_text(ver, "v1.2 - Companion v1.0");
  lv_obj_set_style_text_color(ver, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);

  pop_in(seal, 30, 360);
  fade_in(status, 240);
  fade_in(badge, 300);

  ui_feedback(UI_FB_READ);
  ui_chrome_footer_set_text(s_footer, "BACK  Exit");
}

void ui_companion_pairing_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "COMPANION", COMPANION_ICON);
  s_footer = ui_chrome_footer(s_screen, "BACK  Cancel");

  s_body = make_body(s_screen);
  build_pairing();

  ui_input_set_screen_handler(companion_input, NULL);

  s_phase_timer = lv_timer_create(phase_timer_cb, PAIR_PHASE_MS, NULL);
  lv_timer_set_repeat_count(s_phase_timer, 1);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void phase_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() == s_screen)
    show_success();
  s_phase_timer = NULL;
  (void)timer;
}

static void companion_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press) {
        if (s_phase_timer != NULL) {
          lv_timer_delete(s_phase_timer);
          s_phase_timer = NULL;
        }
        ui_switch_screen(SCREEN_BLE_MENU);
      }
      break;
    default:
      break;
  }
}
