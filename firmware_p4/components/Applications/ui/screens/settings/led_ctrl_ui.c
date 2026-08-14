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

#include "led_ctrl_ui.h"

#include "st7789.h"

#include "buttons_gpio.h"
#include "led_control.h"
#include "notify_ui.h"
#include "tos_config.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 50

#define MX        8
#define CONTENT_W (LCD_H_RES - 2 * MX)

#define PREVIEW_Y      50
#define PREVIEW_D      72
#define CTRL_Y         134
#define CTRL_GAP       8
#define CARD_LABEL_W   66
#define SWATCH_CARD_H  46
#define BRIGHT_CARD_H  42
#define STEALTH_CARD_H 40
#define SWATCH_D       22
#define BAR_H          12
#define PILL_W         52
#define PILL_H         24

#define BRIGHT_MIN     20
#define BRIGHT_MAX     100
#define BRIGHT_STEP    20
#define BRIGHT_DEFAULT 80

#define COL_DIM   0x8A8594
#define COL_OFF   0x101018
#define COL_TRACK 0x202028

#define HDR_ICON  "/assets/icons/palette.bin"
#define HDR_TITLE "LED CONTROL"

enum {
  FOCUS_COLOR = 0,
  FOCUS_BRIGHT,
  FOCUS_STEALTH,
  FOCUS_COUNT,
};

typedef struct {
  const char *name;
  uint32_t hex;
} preset_t;

// Saturated primaries: these drive a physical RGB LED, so pale/pastel values
// (lots of all three channels) wash out to white. Keep the channels pure.
static const preset_t PRESETS[] = {
    {"Red", 0xFF0000},
    {"Green", 0x00FF00},
    {"Blue", 0x0000FF},
    {"Purple", 0x8000FF},
    {"White", 0xFFFFFF},
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_preview = NULL;
static lv_obj_t *s_card[FOCUS_COUNT];
static lv_obj_t *s_swatch[PRESET_COUNT];
static lv_obj_t *s_bright_fill = NULL;
static lv_obj_t *s_bright_val = NULL;
static lv_obj_t *s_pill = NULL;
static lv_obj_t *s_pill_lbl = NULL;
static lv_timer_t *s_nav_timer = NULL;

static int s_focus = FOCUS_COLOR;
static int s_color_idx = 3;
static int s_bright = BRIGHT_DEFAULT;
static bool s_stealth = false;
static bool s_changed = false;

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_left_last = false;
static bool s_right_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

// Scale one 8-bit channel by a percentage, clamped to [0, 255] so an out-of-range
// percentage can never overflow the byte and wrap the color to a wrong hue.
static uint8_t scale_channel(uint8_t chan, int pct) {
  if (pct < 0)
    pct = 0;
  uint32_t v = (uint32_t)chan * (uint32_t)pct / 100;
  return (uint8_t)(v > 255 ? 255 : v);
}

static lv_color_t scaled_color(uint32_t hex, int pct) {
  uint8_t r = scale_channel((hex >> 16) & 0xFF, pct);
  uint8_t g = scale_channel((hex >> 8) & 0xFF, pct);
  uint8_t b = scale_channel(hex & 0xFF, pct);
  return lv_color_make(r, g, b);
}

static void update_preview(void) {
  if (s_preview == NULL)
    return;
  if (s_stealth) {
    lv_obj_set_style_bg_color(s_preview, lv_color_hex(COL_OFF), 0);
    lv_obj_set_style_border_color(s_preview, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_border_width(s_preview, 2, 0);
    lv_obj_set_style_shadow_width(s_preview, 0, 0);
    led_clear(); // stealth: physical LED off
    return;
  }
  lv_color_t c = scaled_color(PRESETS[s_color_idx].hex, s_bright);
  lv_obj_set_style_bg_color(s_preview, c, 0);
  lv_obj_set_style_border_color(s_preview, lv_color_hex(PRESETS[s_color_idx].hex), 0);
  lv_obj_set_style_border_width(s_preview, 2, 0);
  lv_obj_set_style_shadow_width(s_preview, 8 + s_bright / 4, 0);
  lv_obj_set_style_shadow_color(s_preview, lv_color_hex(PRESETS[s_color_idx].hex), 0);
  lv_obj_set_style_shadow_opa(s_preview, LV_OPA_60, 0);

  // Drive the physical RGB LED (LP5816) to match the preview: preset color
  // scaled by the brightness percentage.
  uint32_t hex = PRESETS[s_color_idx].hex;
  led_set_color(scale_channel((hex >> 16) & 0xFF, s_bright),
                scale_channel((hex >> 8) & 0xFF, s_bright),
                scale_channel(hex & 0xFF, s_bright));
}

static void update_swatches(void) {
  for (int i = 0; i < PRESET_COUNT; i++) {
    bool sel = (i == s_color_idx);
    lv_obj_set_style_border_color(
        s_swatch[i], sel ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(s_swatch[i], sel ? 3 : 1, 0);
    lv_obj_set_style_shadow_width(s_swatch[i], sel ? 10 : 0, 0);
    lv_obj_set_style_shadow_color(s_swatch[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_opa(s_swatch[i], sel ? LV_OPA_60 : LV_OPA_TRANSP, 0);
  }
}

static void update_bright(void) {
  if (s_bright_fill)
    lv_obj_set_width(s_bright_fill, lv_pct(s_bright));
  if (s_bright_val)
    lv_label_set_text_fmt(s_bright_val, "%d%%", s_bright);
}

static void update_stealth(void) {
  if (s_pill == NULL)
    return;
  lv_obj_set_style_bg_color(
      s_pill, s_stealth ? current_theme.border_accent : lv_color_hex(COL_TRACK), 0);
  lv_label_set_text(s_pill_lbl, s_stealth ? "ON" : "OFF");
  lv_obj_set_style_text_color(
      s_pill_lbl, s_stealth ? current_theme.screen_base : lv_color_hex(COL_DIM), 0);
}

static void update_focus(void) {
  for (int i = 0; i < FOCUS_COUNT; i++) {
    bool f = (i == s_focus);
    lv_obj_set_style_border_color(
        s_card[i], f ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_width(s_card[i], f ? 14 : 0, 0);
    lv_obj_set_style_shadow_color(s_card[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_spread(s_card[i], f ? -3 : 0, 0);
    lv_obj_set_style_shadow_opa(s_card[i], f ? LV_OPA_50 : LV_OPA_TRANSP, 0);
  }
}

static lv_obj_t *make_ctrl_card(lv_obj_t *parent, const char *label, int h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, CONTENT_W, h);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_hor(card, 10, 0);
  lv_obj_set_style_pad_ver(card, 0, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(card, 8, 0);

  lv_obj_t *lbl = lv_label_create(card);
  lv_label_set_text(lbl, label);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DIM), 0);
  lv_obj_set_width(lbl, CARD_LABEL_W);
  return card;
}

static void build_preview(void) {
  s_preview = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_preview, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_preview, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_preview, PREVIEW_D, PREVIEW_D);
  lv_obj_align(s_preview, LV_ALIGN_TOP_MID, 0, PREVIEW_Y);
  lv_obj_set_style_radius(s_preview, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(s_preview, 0, 0);
}

static void build_color_card(lv_obj_t *parent) {
  lv_obj_t *card = make_ctrl_card(parent, "COLOR", SWATCH_CARD_H);
  s_card[FOCUS_COLOR] = card;

  lv_obj_t *row = lv_obj_create(card);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_flex_grow(row, 1);

  for (int i = 0; i < PRESET_COUNT; i++) {
    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(dot, SWATCH_D, SWATCH_D);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(PRESETS[i].hex), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    s_swatch[i] = dot;
  }
}

static void build_bright_card(lv_obj_t *parent) {
  lv_obj_t *card = make_ctrl_card(parent, "BRIGHT", BRIGHT_CARD_H);
  s_card[FOCUS_BRIGHT] = card;

  lv_obj_t *track = lv_obj_create(card);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_height(track, BAR_H);
  lv_obj_set_flex_grow(track, 1);
  lv_obj_set_style_radius(track, 4, 0);
  lv_obj_set_style_bg_color(track, lv_color_hex(COL_TRACK), 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(track, 1, 0);
  lv_obj_set_style_border_color(track, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_all(track, 0, 0);
  lv_obj_set_style_clip_corner(track, true, 0);

  s_bright_fill = lv_obj_create(track);
  lv_obj_remove_flag(s_bright_fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_bright_fill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_height(s_bright_fill, lv_pct(100));
  lv_obj_set_width(s_bright_fill, lv_pct(s_bright));
  lv_obj_align(s_bright_fill, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(s_bright_fill, 4, 0);
  lv_obj_set_style_border_width(s_bright_fill, 0, 0);
  lv_obj_set_style_bg_color(s_bright_fill, current_theme.border_interface, 0);
  lv_obj_set_style_bg_grad_color(s_bright_fill, current_theme.border_accent, 0);
  lv_obj_set_style_bg_grad_dir(s_bright_fill, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(s_bright_fill, LV_OPA_COVER, 0);

  s_bright_val = lv_label_create(card);
  lv_label_set_text_fmt(s_bright_val, "%d%%", s_bright);
  lv_obj_set_style_text_font(s_bright_val, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_bright_val, current_theme.text_main, 0);
  lv_obj_set_width(s_bright_val, 36);
  lv_obj_set_style_text_align(s_bright_val, LV_TEXT_ALIGN_RIGHT, 0);
}

static void build_stealth_card(lv_obj_t *parent) {
  lv_obj_t *card = make_ctrl_card(parent, "STEALTH", STEALTH_CARD_H);
  s_card[FOCUS_STEALTH] = card;

  lv_obj_t *desc = lv_label_create(card);
  lv_label_set_text(desc, "LED off");
  lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(desc, current_theme.text_main, 0);
  lv_obj_set_flex_grow(desc, 1);

  s_pill = lv_obj_create(card);
  lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_pill, PILL_W, PILL_H);
  lv_obj_set_style_radius(s_pill, PILL_H / 2, 0);
  lv_obj_set_style_border_width(s_pill, 0, 0);
  lv_obj_set_style_bg_color(s_pill, lv_color_hex(COL_TRACK), 0);
  lv_obj_set_style_bg_opa(s_pill, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_pill, 0, 0);

  s_pill_lbl = lv_label_create(s_pill);
  lv_label_set_text(s_pill_lbl, "OFF");
  lv_obj_set_style_text_font(s_pill_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_pill_lbl, lv_color_hex(COL_DIM), 0);
  lv_obj_center(s_pill_lbl);
}

static void build_controls(void) {
  lv_obj_t *col = lv_obj_create(s_screen);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(col, CONTENT_W, LV_SIZE_CONTENT);
  lv_obj_align(col, LV_ALIGN_TOP_MID, 0, CTRL_Y);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_row(col, CTRL_GAP, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  build_color_card(col);
  build_bright_card(col);
  build_stealth_card(col);
}

static void toggle_stealth(void) {
  s_stealth = !s_stealth;
  s_changed = true;
  update_stealth();
  update_preview();
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (back && !s_back_last) {
    if (s_changed)
      notify(NOTIFY_SAVED, "LED settings saved");
    led_clear(); // status LED is normally off: don't leave the preview lit
    ui_switch_screen(SCREEN_INTERFACE_SETTINGS);
    return;
  }
  if (down && !s_down_last) {
    s_focus = (s_focus + 1) % FOCUS_COUNT;
    update_focus();
    ui_feedback(UI_FB_NAV);
  }
  if (up && !s_up_last) {
    s_focus = (s_focus - 1 + FOCUS_COUNT) % FOCUS_COUNT;
    update_focus();
    ui_feedback(UI_FB_NAV);
  }

  if (left && !s_left_last) {
    if (s_focus == FOCUS_COLOR) {
      s_color_idx = (s_color_idx - 1 + PRESET_COUNT) % PRESET_COUNT;
      s_changed = true;
      update_swatches();
      update_preview();
    } else if (s_focus == FOCUS_BRIGHT) {
      int nb = s_bright - BRIGHT_STEP;
      if (nb < BRIGHT_MIN)
        nb = BRIGHT_MIN;
      if (nb != s_bright) {
        s_bright = nb;
        s_changed = true;
        update_bright();
        update_preview();
      }
    } else if (s_focus == FOCUS_STEALTH) {
      toggle_stealth();
    }
  }
  if (right && !s_right_last) {
    if (s_focus == FOCUS_COLOR) {
      s_color_idx = (s_color_idx + 1) % PRESET_COUNT;
      s_changed = true;
      update_swatches();
      update_preview();
    } else if (s_focus == FOCUS_BRIGHT) {
      int nb = s_bright + BRIGHT_STEP;
      if (nb > BRIGHT_MAX)
        nb = BRIGHT_MAX;
      if (nb != s_bright) {
        s_bright = nb;
        s_changed = true;
        update_bright();
        update_preview();
      }
    } else if (s_focus == FOCUS_STEALTH) {
      toggle_stealth();
    }
  }

  if (ok && !s_ok_last) {
    if (s_focus == FOCUS_STEALTH)
      toggle_stealth();
    else
      ui_feedback(UI_FB_SELECT);
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_led_ctrl_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_focus = FOCUS_COLOR;
  s_color_idx = 3;
  s_bright = g_config_led.brightness;
  if (s_bright < BRIGHT_MIN)
    s_bright = BRIGHT_MIN;
  if (s_bright > BRIGHT_MAX)
    s_bright = BRIGHT_MAX;
  s_stealth = false;
  s_changed = false;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  build_preview();
  build_controls();

  update_swatches();
  update_bright();
  update_stealth();
  update_preview();
  update_focus();

  ui_chrome_footer(s_screen, "UP/DOWN pick   L/R adjust   BACK exit");

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}
