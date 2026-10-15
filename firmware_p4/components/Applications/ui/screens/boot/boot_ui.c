// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "boot_ui.h"

#include "lvgl.h"

void ui_boot_show(void) {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(screen);
  lv_obj_remove_style_all(title);
  lv_label_set_text(title, "HIGH BOY");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_letter_space(title, 3, 0);
  lv_obj_center(title);

  lv_screen_load(screen);
  lv_refr_now(NULL);
}

void ui_boot_update_status(int step) {
  // Retain the boot API while keeping the splash free of status indicators.
  (void)step;
}
