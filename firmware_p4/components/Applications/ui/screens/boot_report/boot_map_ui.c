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

#include "boot_map_ui.h"

#include "lvgl.h"

#include "boot_report.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define TITLE     "BOOT MAP"
#define ICON      "/assets/icons/troubleshoot.bin"
#define HINT      "BACK exit"

#define OK_COLOR   0x00E676
#define FAIL_COLOR 0xFF5252
#define SKIP_COLOR 0x8A8594

static lv_obj_t *s_screen = NULL;
static void (*s_on_back)(void) = NULL;

static void boot_map_input(const input_event_t *ev, void *ctx);

static void add_row(lv_obj_t *list, const boot_stage_t *st) {
  lv_obj_t *row = lv_obj_create(list);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 2, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *name = lv_label_create(row);
  lv_label_set_text_fmt(name, "%s%s", st->name, st->required ? " *" : "");
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);

  const char *state;
  uint32_t color;
  if (st->result == ESP_OK) {
    state = "OK";
    color = OK_COLOR;
  } else if (st->result == ESP_ERR_NOT_FOUND) {
    state = "skip";
    color = SKIP_COLOR;
  } else {
    state = esp_err_to_name(st->result);
    color = FAIL_COLOR;
  }

  lv_obj_t *val = lv_label_create(row);
  lv_label_set_text(val, state);
  lv_obj_set_style_text_color(val, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
}

static void build_screen(void) {
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

  lv_obj_t *title = lv_label_create(s_screen);
  lv_label_set_text(title, TITLE);
  lv_obj_set_style_text_color(title, ui_theme_get_accent(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *list = lv_obj_create(s_screen);
  lv_obj_set_size(list, lv_pct(92), lv_pct(72));
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 4, 0);
  lv_obj_set_style_pad_row(list, 2, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  int count = 0;
  const boot_stage_t *stages = boot_report_stages(&count);
  for (int i = 0; i < count; i++) {
    add_row(list, &stages[i]);
  }

  lv_obj_t *hint = lv_label_create(s_screen);
  lv_label_set_text(hint, HINT);
  lv_obj_set_style_text_color(hint, current_theme.border_inactive, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);

  ui_input_set_screen_handler(boot_map_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void boot_map_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->action == INPUT_ACTION_PRESS && ev->button == INPUT_BTN_BACK) {
    if (s_on_back != NULL) {
      s_on_back();
    } else {
      ui_switch_screen(SCREEN_DEV_MENU);
    }
  }
}

void ui_boot_map_open(void) {
  s_on_back = NULL;
  build_screen();
}

void ui_boot_map_open_cb(void (*on_back)(void)) {
  s_on_back = on_back;
  build_screen();
}
