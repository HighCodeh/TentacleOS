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

#include "crash_report_ui.h"

#include <stdio.h>

#include "lvgl.h"

#include "boot_report.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define TITLE "LAST CRASH"

static lv_obj_t *s_screen = NULL;
static void (*s_on_back)(void) = NULL;
static bool s_has_dump = false;

static void crash_report_input(const input_event_t *ev, void *ctx);

static void add_line(lv_obj_t *parent, const char *text, uint32_t color) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  const crash_info_t *c = boot_report_crash();
  s_has_dump = c->has_coredump;

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

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_set_size(body, lv_pct(92), lv_pct(72));
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 4, 0);
  lv_obj_set_style_pad_row(body, 3, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  char buf[48];
  snprintf(buf, sizeof(buf), "Reset: %s", boot_report_reason_str(c->reason));
  add_line(body, buf, 0xFFFFFF);

  snprintf(buf, sizeof(buf), "Panics total: %lu", (unsigned long)boot_report_panic_total());
  add_line(body, buf, 0xE0E0E0);

  uint32_t abnormal = boot_report_abnormal_boots();
  if (abnormal > 0) {
    snprintf(buf,
             sizeof(buf),
             "Abnormal boots: %lu/%d",
             (unsigned long)abnormal,
             BOOT_REPORT_BOOTLOOP_THRESHOLD);
    add_line(body, buf, abnormal >= BOOT_REPORT_BOOTLOOP_THRESHOLD ? 0xFF5252 : 0xFFC23D);
  }

  if (!c->crash) {
    add_line(body, "No crash recorded.", 0x00E676);
  } else {
    if (c->has_coredump) {
      snprintf(buf, sizeof(buf), "Task: %s", c->task[0] ? c->task : "?");
      add_line(body, buf, 0xFF5252);
      snprintf(buf, sizeof(buf), "PC:    0x%08lx", (unsigned long)c->pc);
      add_line(body, buf, 0xE0E0E0);
      snprintf(buf, sizeof(buf), "RA:    0x%08lx", (unsigned long)c->ra);
      add_line(body, buf, 0xE0E0E0);
      snprintf(buf, sizeof(buf), "SP:    0x%08lx", (unsigned long)c->sp);
      add_line(body, buf, 0xE0E0E0);
      snprintf(buf, sizeof(buf), "mcause 0x%08lx", (unsigned long)c->mcause);
      add_line(body, buf, 0xE0E0E0);
      snprintf(buf, sizeof(buf), "mtval  0x%08lx", (unsigned long)c->mtval);
      add_line(body, buf, 0xE0E0E0);
    } else {
      add_line(body, "No core dump image.", 0xFFC23D);
    }
  }

  lv_obj_t *hint = lv_label_create(s_screen);
  lv_label_set_text(hint, s_has_dump ? "OK clear   BACK exit" : "BACK exit");
  lv_obj_set_style_text_color(hint, current_theme.border_inactive, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);

  ui_input_set_screen_handler(crash_report_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void crash_report_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->action != INPUT_ACTION_PRESS) {
    return;
  }
  if (ev->button == INPUT_BTN_OK && s_has_dump) {
    boot_report_clear_crash();
    build_screen(); // redraw: dump is gone now
  } else if (ev->button == INPUT_BTN_BACK) {
    if (s_on_back != NULL) {
      s_on_back();
    } else {
      ui_switch_screen(SCREEN_DEV_MENU);
    }
  }
}

void ui_crash_report_open(void) {
  s_on_back = NULL;
  build_screen();
}

void ui_crash_report_open_cb(void (*on_back)(void)) {
  s_on_back = on_back;
  build_screen();
}
