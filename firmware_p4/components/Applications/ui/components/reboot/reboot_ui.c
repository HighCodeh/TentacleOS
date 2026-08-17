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

#include "reboot_ui.h"

#include "esp_log.h"
#include "esp_system.h"
#include "lvgl.h"

#include "ui_manager.h"

static const char *TAG = "REBOOT_UI";

#define REBOOT_UI_LABEL      LV_SYMBOL_REFRESH "  Rebooting..."
#define REBOOT_UI_TEXT_COLOR 0x8A8594
#define REBOOT_UI_HOLD_MS    450

static void reboot_fire_cb(lv_timer_t *timer);

void reboot_ui_show(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, REBOOT_UI_LABEL);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(REBOOT_UI_TEXT_COLOR), 0);
  lv_obj_center(lbl);

  ui_screen_load(scr);
}

void reboot_ui_reboot(void) {
  ESP_LOGI(TAG, "Graceful reboot requested");
  reboot_ui_show();

  lv_timer_t *timer = lv_timer_create(reboot_fire_cb, REBOOT_UI_HOLD_MS, NULL);
  lv_timer_set_repeat_count(timer, 1);
}

static void reboot_fire_cb(lv_timer_t *timer) {
  lv_timer_delete(timer);
  esp_restart();
}
