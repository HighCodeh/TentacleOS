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

#include "gpio_ui.h"

#include "esp_log.h"
#include "lvgl.h"

#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "GPIO_UI";

#define HEADER_TITLE "GPIO"
#define HINT_TEXT    "OK = Toggle   BACK = Exit"

#define ON_COLOR   0x00E676
#define HOLE_COLOR 0x05030C

#define BODY_W      202
#define ROW_H       26
#define ROW_GAP     6
#define BODY_PAD    12
#define BODY_RADIUS 12
#define BODY_Y      (-2)

#define PAD_SIZE  18
#define PAD_HOLE  7
#define COL_INSET 10
#define LABEL_GAP 36

#define SEL_BORDER        2
#define PULSE_BORDER_PEAK 5

#define RAIL_W   2
#define RAIL_OPA LV_OPA_20

#define STATUS_GAP   12
#define BODY_FADE_MS 220
#define PULSE_MS     150

#define COLS 2

static const struct {
  const char *name;
  const char *tag;
  bool on;
} PINS[] = {
    {"PIN 1  (IO1)", "IO1", false},
    {"PIN 2  (IO2)", "IO2", true},
    {"PIN 3  (IO3)", "IO3", false},
    {"PIN 4  (IO4)", "IO4", false},
    {"PIN 5  (IO5)", "IO5", true},
    {"PIN 6  (IO6)", "IO6", false},
    {"PIN 7  (IO7)", "IO7", false},
    {"PIN 8  (IO8)", "IO8", false},
    {"5V on pin 1", "5V", false},
    {"USB-UART bridge", "UART", false},
};
#define PIN_COUNT    ((int)(sizeof(PINS) / sizeof(PINS[0])))
#define ROWS_PER_COL ((PIN_COUNT + COLS - 1) / COLS)

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_body = NULL;
static lv_obj_t *s_pad[PIN_COUNT];
static lv_obj_t *s_label[PIN_COUNT];
static lv_obj_t *s_status = NULL;

static bool s_on[PIN_COUNT];
static int s_sel = 0;

static void opa_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void border_width_cb(void *var, int32_t v) {
  lv_obj_set_style_border_width((lv_obj_t *)var, v, 0);
}

static void apply_pin_state(int i) {
  if (i < 0 || i >= PIN_COUNT)
    return;
  bool on = s_on[i];
  lv_color_t on_color = lv_color_hex(ON_COLOR);

  lv_obj_set_style_bg_color(s_pad[i], on ? on_color : current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(s_pad[i], LV_OPA_COVER, 0);

  lv_obj_set_style_text_color(s_label[i], on ? on_color : current_theme.text_main, 0);
  lv_obj_set_style_text_opa(s_label[i], on ? LV_OPA_COVER : LV_OPA_80, 0);
}

static void apply_selection(void) {
  for (int i = 0; i < PIN_COUNT; i++) {
    bool sel = (i == s_sel);
    bool on = s_on[i];
    lv_obj_set_style_border_width(s_pad[i], sel ? SEL_BORDER : 0, 0);
    lv_obj_set_style_border_color(s_pad[i], current_theme.border_accent, 0);
    lv_obj_set_style_border_opa(s_pad[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

    if (on) {
      lv_obj_set_style_shadow_color(s_pad[i], lv_color_hex(ON_COLOR), 0);
      lv_obj_set_style_shadow_width(s_pad[i], 12, 0);
      lv_obj_set_style_shadow_opa(s_pad[i], LV_OPA_60, 0);
      lv_obj_set_style_shadow_spread(s_pad[i], 0, 0);
    } else if (sel) {
      lv_obj_set_style_shadow_color(s_pad[i], current_theme.border_accent, 0);
      lv_obj_set_style_shadow_width(s_pad[i], 10, 0);
      lv_obj_set_style_shadow_opa(s_pad[i], LV_OPA_50, 0);
      lv_obj_set_style_shadow_spread(s_pad[i], -2, 0);
    } else {
      lv_obj_set_style_shadow_width(s_pad[i], 0, 0);
      lv_obj_set_style_shadow_opa(s_pad[i], LV_OPA_TRANSP, 0);
    }
  }
}

static void update_status(void) {
  bool on = s_on[s_sel];
  lv_label_set_text_fmt(s_status, "%s   %s", PINS[s_sel].name, on ? "HIGH" : "LOW");
  lv_obj_set_style_text_color(s_status, on ? lv_color_hex(ON_COLOR) : current_theme.text_main, 0);
  lv_obj_set_style_text_opa(s_status, on ? LV_OPA_COVER : LV_OPA_60, 0);
}

static void pulse_pin(int i) {
  if (i < 0 || i >= PIN_COUNT)
    return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_pad[i]);
  lv_anim_set_exec_cb(&a, border_width_cb);
  lv_anim_set_values(&a, SEL_BORDER, PULSE_BORDER_PEAK);
  lv_anim_set_duration(&a, PULSE_MS);
  lv_anim_set_playback_duration(&a, PULSE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void make_pin_row(int i) {
  int col = i / ROWS_PER_COL;
  int row = i % ROWS_PER_COL;
  bool left = (col == 0);

  int avail = ROWS_PER_COL * ROW_H + (ROWS_PER_COL - 1) * ROW_GAP;
  int y = -avail / 2 + row * (ROW_H + ROW_GAP) + ROW_H / 2;

  lv_obj_t *pad = lv_obj_create(s_body);
  lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(pad, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(pad, PAD_SIZE, PAD_SIZE);
  lv_obj_set_style_radius(pad, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(pad, 0, 0);
  lv_obj_set_style_pad_all(pad, 0, 0);
  lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
  lv_obj_align(
      pad, left ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, left ? COL_INSET : -COL_INSET, y);
  s_pad[i] = pad;

  lv_obj_t *hole = lv_obj_create(pad);
  lv_obj_remove_flag(hole, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hole, PAD_HOLE, PAD_HOLE);
  lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(hole, 0, 0);
  lv_obj_set_style_pad_all(hole, 0, 0);
  lv_obj_set_style_bg_color(hole, lv_color_hex(HOLE_COLOR), 0);
  lv_obj_set_style_bg_opa(hole, LV_OPA_70, 0);
  lv_obj_center(hole);

  lv_obj_t *lbl = lv_label_create(s_body);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  lv_label_set_text(lbl, PINS[i].tag);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_width(lbl, BODY_W / 2 - LABEL_GAP - BODY_PAD);
  lv_obj_set_style_text_align(lbl, left ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(
      lbl, left ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, left ? LABEL_GAP : -LABEL_GAP, y);
  s_label[i] = lbl;
}

static void build_connector(void) {
  int body_h = ROWS_PER_COL * ROW_H + (ROWS_PER_COL - 1) * ROW_GAP + BODY_PAD * 2;

  s_body = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_body, BODY_W, body_h);
  lv_obj_align(s_body, LV_ALIGN_CENTER, 0, BODY_Y);
  lv_obj_set_style_pad_all(s_body, BODY_PAD, 0);
  lv_obj_set_style_radius(s_body, BODY_RADIUS, 0);
  lv_obj_set_style_bg_color(s_body, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(s_body, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_body, 1, 0);
  lv_obj_set_style_border_color(s_body, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(s_body, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(s_body, 14, 0);
  lv_obj_set_style_shadow_opa(s_body, LV_OPA_30, 0);
  lv_obj_set_style_shadow_spread(s_body, -4, 0);

  lv_obj_t *rail = lv_obj_create(s_body);
  lv_obj_remove_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(rail, RAIL_W, body_h - BODY_PAD * 2);
  lv_obj_center(rail);
  lv_obj_set_style_border_width(rail, 0, 0);
  lv_obj_set_style_radius(rail, 1, 0);
  lv_obj_set_style_bg_color(rail, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(rail, RAIL_OPA, 0);

  for (int i = 0; i < PIN_COUNT; i++)
    make_pin_row(i);
}

static void toggle_selected(void) {
  s_on[s_sel] = !s_on[s_sel];
  apply_pin_state(s_sel);
  apply_selection();
  pulse_pin(s_sel);
  update_status();
  ESP_LOGI(TAG, "mock toggle pin %d -> %d", s_sel, s_on[s_sel]);
}

static void move_selection(int dir) {
  s_sel = (s_sel + dir + PIN_COUNT) % PIN_COUNT;
  apply_selection();
  update_status();
}

static void gpio_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        move_selection(1);
      break;
    case INPUT_BTN_UP:
      if (nav)
        move_selection(-1);
      break;
    case INPUT_BTN_OK:
      if (press)
        toggle_selected();
      break;
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    default:
      break;
  }
}

void ui_gpio_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_sel = 0;
  for (int i = 0; i < PIN_COUNT; i++)
    s_on[i] = PINS[i].on;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, HEADER_TITLE, "/assets/icons/developer_board.bin");
  build_connector();

  for (int i = 0; i < PIN_COUNT; i++)
    apply_pin_state(i);
  apply_selection();

  s_status = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
  lv_obj_align_to(s_status, s_body, LV_ALIGN_OUT_BOTTOM_MID, 0, STATUS_GAP);
  update_status();

  ui_chrome_footer(s_screen, HINT_TEXT);

  lv_obj_set_style_opa(s_body, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_body);
  lv_anim_set_exec_cb(&a, opa_anim_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, BODY_FADE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);

  ui_input_set_screen_handler(gpio_input, NULL);

  ui_screen_load(s_screen);
}
