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

#include "led_control.h"
#include "notify_ui.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

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

#define BRIGHT_MIN     5 // lowest level the LED still lights at
#define BRIGHT_MAX     100
#define BRIGHT_STEP    5
#define BRIGHT_DEFAULT 80

#define COL_DIM   0x8A8594
#define COL_OFF   0x101018
#define COL_TRACK 0x202028

#define HDR_ICON  "/assets/icons/palette.bin"
#define HDR_TITLE "LED SIGNALS"

// The three semantic signals whose color is being configured.
enum { SIG_INFO = 0, SIG_WARNING, SIG_ERROR, SIG_COUNT };
static const char *SIG_NAMES[SIG_COUNT] = {"INFO", "WARNING", "ERROR"};

enum {
  FOCUS_SIGNAL = 0, // which signal to edit (info / warning / error)
  FOCUS_COLOR,      // color assigned to the selected signal
  FOCUS_BRIGHT,     // global intensity (one value for all signals)
  FOCUS_COUNT,
};

typedef struct {
  const char *name;
  uint32_t hex;
} preset_t;

// Saturated primaries: these drive a physical RGB LED, so pale/pastel values
// (lots of all three channels) wash out to white. Keep the channels pure. The
// three signal defaults (purple/yellow/red) are all present so they map back.
static const preset_t PRESETS[] = {
    {"Red", 0xFF0000},
    {"Orange", 0xFF6000},
    {"Yellow", 0xFFFF00},
    {"Green", 0x00FF00},
    {"Blue", 0x0000FF},
    {"Purple", 0xFF00FF},
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_preview = NULL;
static lv_obj_t *s_card[FOCUS_COUNT];
static lv_obj_t *s_swatch[PRESET_COUNT];
static lv_obj_t *s_bright_fill = NULL;
static lv_obj_t *s_bright_val = NULL;
static lv_obj_t *s_sig_val = NULL;

static int s_focus = FOCUS_SIGNAL;
static int s_signal = SIG_INFO;
static int s_color_idx[SIG_COUNT]; // selected preset index per signal
static int s_bright = BRIGHT_DEFAULT;
static bool s_changed = false;

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

static uint32_t current_hex(void) {
  return PRESETS[s_color_idx[s_signal]].hex;
}

static void update_preview(void) {
  if (s_preview == NULL)
    return;
  uint32_t hex = current_hex();
  lv_color_t c = scaled_color(hex, s_bright);
  lv_obj_set_style_bg_color(s_preview, c, 0);
  lv_obj_set_style_border_color(s_preview, lv_color_hex(hex), 0);
  lv_obj_set_style_border_width(s_preview, 2, 0);
  lv_obj_set_style_shadow_width(s_preview, 8 + s_bright / 4, 0);
  lv_obj_set_style_shadow_color(s_preview, lv_color_hex(hex), 0);
  lv_obj_set_style_shadow_opa(s_preview, LV_OPA_60, 0);

  // Drive the physical RGB LED (LP5816) to match the preview: selected signal's
  // color scaled by the brightness percentage.
  led_set_color(scale_channel((hex >> 16) & 0xFF, s_bright),
                scale_channel((hex >> 8) & 0xFF, s_bright),
                scale_channel(hex & 0xFF, s_bright));
}

static void update_swatches(void) {
  for (int i = 0; i < PRESET_COUNT; i++) {
    bool sel = (i == s_color_idx[s_signal]);
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

static void update_signal(void) {
  if (s_sig_val == NULL)
    return;
  lv_label_set_text(s_sig_val, SIG_NAMES[s_signal]);
  lv_obj_set_style_text_color(s_sig_val, lv_color_hex(current_hex()), 0);
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

static void build_signal_card(lv_obj_t *parent) {
  lv_obj_t *card = make_ctrl_card(parent, "SIGNAL", STEALTH_CARD_H);
  s_card[FOCUS_SIGNAL] = card;

  s_sig_val = lv_label_create(card);
  lv_label_set_text(s_sig_val, SIG_NAMES[s_signal]);
  lv_obj_set_style_text_font(s_sig_val, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(s_sig_val, current_theme.text_main, 0);
  lv_obj_set_flex_grow(s_sig_val, 1);
  lv_obj_set_style_text_align(s_sig_val, LV_TEXT_ALIGN_RIGHT, 0);
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

  build_signal_card(col);
  build_color_card(col);
  build_bright_card(col);
}

static int preset_index_of(uint32_t hex) {
  for (int i = 0; i < PRESET_COUNT; i++) {
    if (PRESETS[i].hex == (hex & 0xFFFFFF))
      return i;
  }
  return 0; // custom/unknown color falls back to the first preset
}

static void save_config(void) {
  g_config_led.brightness = s_bright;
  g_config_led.info_color = PRESETS[s_color_idx[SIG_INFO]].hex;
  g_config_led.warning_color = PRESETS[s_color_idx[SIG_WARNING]].hex;
  g_config_led.error_color = PRESETS[s_color_idx[SIG_ERROR]].hex;

  // Apply live so subsequent signals use the new colors/brightness immediately.
  led_set_signal_config(g_config_led.info_color,
                        g_config_led.warning_color,
                        g_config_led.error_color,
                        g_config_led.brightness);

  // Persist to SD only (matches the storage policy: no SD -> keep in RAM for this
  // session but do not write anything).
  if (tos_config_save(TOS_PATH_CONFIG_LED, "led") == ESP_OK) {
    notify(NOTIFY_SAVED, "LED settings saved");
  } else {
    notify(NOTIFY_WARNING, "No SD: not saved");
  }
}

static void led_ctrl_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press) {
        if (s_changed)
          save_config();
        led_clear();
        ui_switch_screen(SCREEN_INTERFACE_SETTINGS);
      }
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        s_focus = (s_focus + 1) % FOCUS_COUNT;
        update_focus();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_focus = (s_focus - 1 + FOCUS_COUNT) % FOCUS_COUNT;
        update_focus();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_LEFT:
      if (press) {
        if (s_focus == FOCUS_SIGNAL) {
          s_signal = (s_signal - 1 + SIG_COUNT) % SIG_COUNT;
          update_signal();
          update_swatches();
          update_preview();
        } else if (s_focus == FOCUS_COLOR) {
          s_color_idx[s_signal] = (s_color_idx[s_signal] - 1 + PRESET_COUNT) % PRESET_COUNT;
          s_changed = true;
          update_swatches();
          update_signal();
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
        }
      }
      break;
    case INPUT_BTN_RIGHT:
      if (press) {
        if (s_focus == FOCUS_SIGNAL) {
          s_signal = (s_signal + 1) % SIG_COUNT;
          update_signal();
          update_swatches();
          update_preview();
        } else if (s_focus == FOCUS_COLOR) {
          s_color_idx[s_signal] = (s_color_idx[s_signal] + 1) % PRESET_COUNT;
          s_changed = true;
          update_swatches();
          update_signal();
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
        }
      }
      break;
    case INPUT_BTN_OK:
      if (press)
        ui_feedback(UI_FB_SELECT);
      break;
    default:
      break;
  }
}

void ui_led_ctrl_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_focus = FOCUS_SIGNAL;
  s_signal = SIG_INFO;
  s_color_idx[SIG_INFO] = preset_index_of(g_config_led.info_color);
  s_color_idx[SIG_WARNING] = preset_index_of(g_config_led.warning_color);
  s_color_idx[SIG_ERROR] = preset_index_of(g_config_led.error_color);
  s_bright = g_config_led.brightness;
  if (s_bright < BRIGHT_MIN)
    s_bright = BRIGHT_MIN;
  if (s_bright > BRIGHT_MAX)
    s_bright = BRIGHT_MAX;
  s_changed = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  build_preview();
  build_controls();

  update_signal();
  update_swatches();
  update_bright();
  update_preview();
  update_focus();

  ui_chrome_footer(s_screen, "UP/DOWN pick   L/R adjust   BACK save");

  ui_input_set_screen_handler(led_ctrl_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
