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

#include <stdio.h>

#include "lvgl.h"
#include "st7789.h"

#include "gpio_header.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define HDR_ICON "/assets/icons/developer_board.bin"
#define GPIO_HINT \
  LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " mode   " LV_SYMBOL_OK " act   BACK exit"

#define POLL_MS  250
#define PWM_STEP 25
#define PWM_FULL 100

#define ROW_H    40
#define CHIP_H   18
#define LIST_PAD 6
#define ROW_GAP  5
#define SEL_GLOW 8
#define WARN_LEN 48

#define COL_FREE   0x00E676
#define COL_PERIPH 0xFF8A3D
#define COL_SPI    0x3B9EFF
#define COL_I2C    0x17C7A6
#define COL_LOCKED 0xFF4D4D
#define COL_POWER  0xFFB020
#define COL_GND    0x55636F

typedef struct {
  lv_obj_t *row;
  lv_obj_t *chip;
  lv_obj_t *chip_lbl;
  lv_obj_t *pill;
  lv_obj_t *pill_lbl;
  int hdr;
} row_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list = NULL;
static lv_timer_t *s_poll = NULL;
static row_t s_rows[GPIO_HEADER_COUNT];
static int s_row_count = 0;
static int s_sel = 0;
static int s_row_lvl[GPIO_HEADER_COUNT];

static const gpio_hdr_mode_t MODE_SEQ[] = {
    GPIO_HDR_MODE_INPUT, GPIO_HDR_MODE_OUTPUT, GPIO_HDR_MODE_PWM, GPIO_HDR_MODE_HIZ};
#define MODE_SEQ_LEN 4

static lv_color_t class_bar(gpio_hdr_class_t k) {
  switch (k) {
    case GPIO_HDR_FREE:
      return lv_color_hex(COL_FREE);
    case GPIO_HDR_PERIPH:
      return lv_color_hex(COL_PERIPH);
    case GPIO_HDR_SPI_BUS:
      return lv_color_hex(COL_SPI);
    case GPIO_HDR_I2C_BUS:
      return lv_color_hex(COL_I2C);
    case GPIO_HDR_CONSOLE:
    case GPIO_HDR_LOCKED:
      return lv_color_hex(COL_LOCKED);
    case GPIO_HDR_POWER:
      return lv_color_hex(COL_POWER);
    default:
      return lv_color_hex(COL_GND);
  }
}

static const char *mode_short(gpio_hdr_mode_t m) {
  switch (m) {
    case GPIO_HDR_MODE_OUTPUT:
      return "OUT";
    case GPIO_HDR_MODE_PWM:
      return "PWM";
    case GPIO_HDR_MODE_HIZ:
      return "HiZ";
    default:
      return "IN";
  }
}

static lv_obj_t *make_chip(lv_obj_t *parent, lv_obj_t **out_lbl) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(c, CHIP_H);
  lv_obj_set_width(c, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(c, 4, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_pad_top(c, 0, 0);
  lv_obj_set_style_pad_bottom(c, 0, 0);
  lv_obj_set_style_pad_left(c, 6, 0);
  lv_obj_set_style_pad_right(c, 6, 0);
  lv_obj_t *l = lv_label_create(c);
  lv_obj_center(l);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  *out_lbl = l;
  return c;
}

static void set_chip(row_t *r, const char *txt, lv_color_t col) {
  lv_obj_remove_flag(r->chip, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(r->chip_lbl, txt);
  lv_obj_set_style_text_color(r->chip_lbl, col, 0);
  lv_obj_set_style_border_color(r->chip, col, 0);
}

static void hide_chip(row_t *r) {
  lv_obj_add_flag(r->chip, LV_OBJ_FLAG_HIDDEN);
}

static void set_pill(row_t *r, const char *txt, lv_color_t fg, lv_color_t bg, bool is_filled) {
  lv_obj_remove_flag(r->pill, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(r->pill_lbl, txt);
  lv_obj_set_style_text_color(r->pill_lbl, fg, 0);
  lv_obj_set_style_bg_color(r->pill, bg, 0);
  lv_obj_set_style_bg_opa(r->pill, is_filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(r->pill, is_filled ? 0 : 1, 0);
  lv_obj_set_style_border_color(r->pill, fg, 0);
}

static void hide_pill(row_t *r) {
  lv_obj_add_flag(r->pill, LV_OBJ_FLAG_HIDDEN);
}

static void update_row(int i) {
  row_t *r = &s_rows[i];
  s_row_lvl[i] = -1;
  if (!gpio_header_is_actionable(r->hdr)) {
    hide_chip(r);
    gpio_hdr_class_t k = gpio_header_info(r->hdr)->klass;
    if (k == GPIO_HDR_LOCKED || k == GPIO_HDR_CONSOLE)
      set_pill(r, "LOCK", lv_color_hex(COL_LOCKED), lv_color_hex(COL_LOCKED), false);
    else
      hide_pill(r);
    return;
  }
  lv_color_t accent = current_theme.border_accent;
  lv_color_t dim = current_theme.border_inactive;
  lv_color_t base = current_theme.screen_base;
  gpio_hdr_mode_t m = gpio_header_mode(r->hdr);
  set_chip(r, mode_short(m), accent);
  if (m == GPIO_HDR_MODE_OUTPUT) {
    if (gpio_header_level(r->hdr))
      set_pill(r, "HI", base, accent, true);
    else
      set_pill(r, "LO", dim, dim, false);
  } else if (m == GPIO_HDR_MODE_PWM) {
    char b[8];
    snprintf(b, sizeof b, "%u%%", gpio_header_pwm_get(r->hdr));
    set_pill(r, b, accent, accent, false);
  } else if (m == GPIO_HDR_MODE_HIZ) {
    set_pill(r, "z", dim, dim, false);
  } else {
    int rd = gpio_header_read(r->hdr);
    s_row_lvl[i] = rd;
    if (rd > 0)
      set_pill(r, "HI", base, accent, true);
    else
      set_pill(r, "LO", dim, dim, false);
  }
}

static void update_all(void) {
  for (int i = 0; i < s_row_count; i++)
    update_row(i);
}

static void apply_selection(void) {
  for (int i = 0; i < s_row_count; i++) {
    row_t *r = &s_rows[i];
    bool is_act = gpio_header_is_actionable(r->hdr);
    bool is_sel = (i == s_sel);
    lv_obj_set_style_bg_color(
        r->row, is_sel ? current_theme.bg_item_top : current_theme.bg_secondary, 0);
    lv_obj_set_style_opa(r->row, is_act ? LV_OPA_COVER : LV_OPA_60, 0);
    if (is_sel) {
      lv_obj_set_style_shadow_color(r->row, current_theme.border_accent, 0);
      lv_obj_set_style_shadow_width(r->row, SEL_GLOW, 0);
      lv_obj_set_style_shadow_opa(r->row, LV_OPA_40, 0);
      lv_obj_set_style_shadow_spread(r->row, -2, 0);
    } else {
      lv_obj_set_style_shadow_width(r->row, 0, 0);
      lv_obj_set_style_shadow_opa(r->row, LV_OPA_TRANSP, 0);
    }
  }
  if (s_row_count > 0)
    lv_obj_scroll_to_view(s_rows[s_sel].row, LV_ANIM_ON);
}

static void make_row(int hdr) {
  const gpio_hdr_info_t *inf = gpio_header_info(hdr);
  bool is_act = gpio_header_is_actionable(hdr);
  int i = s_row_count;

  lv_obj_t *row = lv_obj_create(s_list);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, ROW_H);
  lv_obj_set_style_radius(row, 8, 0);
  lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 3, 0);
  lv_obj_set_style_border_color(row, class_bar(inf->klass), 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_pad_top(row, 0, 0);
  lv_obj_set_style_pad_bottom(row, 0, 0);
  lv_obj_set_style_pad_left(row, 10, 0);
  lv_obj_set_style_pad_right(row, 8, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);

  lv_obj_t *txt = lv_obj_create(row);
  lv_obj_remove_flag(txt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(txt, 0, 0);
  lv_obj_set_style_pad_all(txt, 0, 0);
  lv_obj_set_style_pad_row(txt, 0, 0);
  lv_obj_set_height(txt, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(txt, 1);
  lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *title = lv_label_create(txt);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, is_act ? current_theme.text_main : current_theme.text_secondary,
                              0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, lv_pct(100));
  lv_label_set_text(title, inf->label);

  lv_obj_t *sub = lv_label_create(txt);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(sub, current_theme.text_secondary, 0);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
  lv_obj_set_width(sub, lv_pct(100));
  lv_label_set_text_fmt(sub, "pin %u  %s", inf->pin, inf->func);

  lv_obj_t *chip_lbl = NULL;
  lv_obj_t *pill_lbl = NULL;
  lv_obj_t *chip = make_chip(row, &chip_lbl);
  lv_obj_t *pill = make_chip(row, &pill_lbl);

  s_rows[i].row = row;
  s_rows[i].chip = chip;
  s_rows[i].chip_lbl = chip_lbl;
  s_rows[i].pill = pill;
  s_rows[i].pill_lbl = pill_lbl;
  s_rows[i].hdr = hdr;
  s_row_count = i + 1;

  update_row(i);
}

static void build_list(void) {
  s_row_count = 0;
  for (int hdr = 0; hdr < gpio_header_count(); hdr++)
    make_row(hdr);
}

static void warn_risky(const gpio_hdr_info_t *inf) {
  if (inf->klass == GPIO_HDR_FREE)
    return;
  char msg[WARN_LEN];
  snprintf(msg, sizeof msg, "%s - %s", inf->label, inf->owner);
  notify(NOTIFY_WARNING, msg);
}

static void do_ok(int hdr) {
  if (!gpio_header_is_actionable(hdr))
    return;
  gpio_hdr_mode_t m = gpio_header_mode(hdr);
  if (m == GPIO_HDR_MODE_OUTPUT) {
    gpio_header_write(hdr, !gpio_header_level(hdr));
    ui_feedback(UI_FB_SELECT);
  } else if (m == GPIO_HDR_MODE_PWM) {
    uint8_t d = gpio_header_pwm_get(hdr);
    d = (uint8_t)(d + PWM_STEP > PWM_FULL ? 0 : d + PWM_STEP);
    gpio_header_pwm_duty(hdr, d);
  }
  update_all();
}

static void do_cycle(int hdr, int dir) {
  if (!gpio_header_is_actionable(hdr))
    return;
  const gpio_hdr_info_t *inf = gpio_header_info(hdr);
  gpio_hdr_mode_t cur = gpio_header_mode(hdr);
  int ci = 0;
  for (int i = 0; i < MODE_SEQ_LEN; i++)
    if (MODE_SEQ[i] == cur) {
      ci = i;
      break;
    }
  gpio_hdr_mode_t next = MODE_SEQ[(ci + dir + MODE_SEQ_LEN) % MODE_SEQ_LEN];

  bool was_driving = (cur == GPIO_HDR_MODE_OUTPUT || cur == GPIO_HDR_MODE_PWM);
  bool now_driving = (next == GPIO_HDR_MODE_OUTPUT || next == GPIO_HDR_MODE_PWM);
  if (now_driving && !was_driving)
    warn_risky(inf);

  gpio_header_set_mode(hdr, next);
  update_all();
}

static void move_sel(int dir) {
  if (s_row_count == 0)
    return;
  int i = s_sel;
  for (int step = 0; step < s_row_count; step++) {
    i = (i + dir + s_row_count) % s_row_count;
    if (gpio_header_is_actionable(s_rows[i].hdr)) {
      s_sel = i;
      break;
    }
  }
  apply_selection();
  ui_feedback(UI_FB_NAV);
}

static void gpio_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  int hdr = (s_sel >= 0 && s_sel < s_row_count) ? s_rows[s_sel].hdr : -1;
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        move_sel(1);
      break;
    case INPUT_BTN_UP:
      if (nav)
        move_sel(-1);
      break;
    case INPUT_BTN_LEFT:
      if (press)
        do_cycle(hdr, -1);
      break;
    case INPUT_BTN_RIGHT:
      if (press)
        do_cycle(hdr, 1);
      break;
    case INPUT_BTN_OK:
      if (press)
        do_ok(hdr);
      break;
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    default:
      break;
  }
}

static void poll_live(void) {
  for (int i = 0; i < s_row_count; i++) {
    int hdr = s_rows[i].hdr;
    if (!gpio_header_is_actionable(hdr))
      continue;
    if (gpio_header_mode(hdr) != GPIO_HDR_MODE_INPUT)
      continue;
    int rd = gpio_header_read(hdr);
    if (rd == s_row_lvl[i])
      continue;
    s_row_lvl[i] = rd;
    if (rd > 0)
      set_pill(&s_rows[i], "HI", current_theme.screen_base, current_theme.border_accent, true);
    else
      set_pill(&s_rows[i], "LO", current_theme.border_inactive, current_theme.border_inactive,
               false);
  }
}

static void poll_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_poll = NULL;
    return;
  }
  poll_live();
}

static void on_delete(lv_event_t *e) {
  (void)e;
  if (s_poll) {
    lv_timer_delete(s_poll);
    s_poll = NULL;
  }
  gpio_header_restore_all();
}

void ui_gpio_open(void) {
  gpio_header_begin();
  s_sel = 0;
  s_row_count = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "GPIO", HDR_ICON);

  s_list = lv_obj_create(s_screen);
  lv_obj_set_size(s_list, LCD_H_RES, LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_list, 0, 0);
  lv_obj_set_style_pad_all(s_list, LIST_PAD, 0);
  lv_obj_set_style_pad_row(s_list, ROW_GAP, 0);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

  build_list();
  for (int i = 0; i < s_row_count; i++)
    if (gpio_header_is_actionable(s_rows[i].hdr)) {
      s_sel = i;
      break;
    }
  apply_selection();

  ui_chrome_footer(s_screen, GPIO_HINT);

  s_poll = lv_timer_create(poll_cb, POLL_MS, NULL);
  lv_obj_add_event_cb(s_screen, on_delete, LV_EVENT_DELETE, NULL);
  ui_input_set_screen_handler(gpio_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}
