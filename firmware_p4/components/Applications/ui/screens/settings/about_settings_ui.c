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

#include "about_settings_ui.h"

#include <stdint.h>
#include <stdio.h>

#include "esp_mac.h"
#include "esp_timer.h"

#include "assets_manager.h"
#include "ota_service.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "ABOUT_SETTINGS_UI";

#define ENTRY_FADE_MS  240
#define HERO_IMG       56
#define CHIP_RADIUS    10
#define CONTENT_WIDTH  214
#define CHIP_ROW_WIDTH 210

static lv_obj_t *s_screen = NULL;
static lv_font_t *s_title_font = NULL;

static void add_chip(lv_obj_t *parent, const char *text, bool accent) {
  lv_obj_t *chip = lv_label_create(parent);
  lv_label_set_text(chip, text);
  lv_obj_set_style_text_font(chip, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(
      chip, accent ? current_theme.border_accent : current_theme.text_main, 0);
  lv_obj_set_style_bg_color(chip, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(chip, CHIP_RADIUS, 0);
  lv_obj_set_style_pad_hor(chip, 9, 0);
  lv_obj_set_style_pad_ver(chip, 4, 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_border_color(chip, current_theme.border_inactive, 0);
}

static void about_settings_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_SETTINGS);
      break;
    default:
      break;
  }
}

void ui_about_settings_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  (void)TAG;

  if (s_title_font == NULL)
    s_title_font = lv_binfont_create("A:assets/fonts/Inter.bin");

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "ABOUT", "/assets/icons/info.bin");
  ui_chrome_footer(s_screen, "BACK: EXIT");

  lv_obj_t *col = lv_obj_create(s_screen);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, CONTENT_WIDTH, LV_SIZE_CONTENT);
  lv_obj_align(col, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 14);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 4, 0);

  lv_image_dsc_t *octobit = assets_get("/assets/img/octobit_portrait.bin");
  if (octobit != NULL) {
    lv_obj_t *img = lv_image_create(col);
    lv_image_set_src(img, octobit);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_set_size(img, HERO_IMG, HERO_IMG);
  }

  lv_obj_t *name = lv_label_create(col);
  lv_label_set_text(name, "HighBoy V2");
  lv_obj_set_style_text_font(name, s_title_font ? s_title_font : &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  lv_obj_set_style_pad_top(name, 4, 0);

  char fwbuf[32];
  snprintf(fwbuf, sizeof(fwbuf), "FW %s", ota_get_current_version());
  lv_obj_t *fw = lv_label_create(col);
  lv_label_set_text(fw, fwbuf);
  lv_obj_set_style_text_font(fw, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(fw, current_theme.border_accent, 0);
  lv_obj_set_style_pad_bottom(fw, 4, 0);

  lv_obj_t *chips = lv_obj_create(col);
  lv_obj_remove_style_all(chips);
  lv_obj_set_size(chips, CHIP_ROW_WIDTH, LV_SIZE_CONTENT);
  lv_obj_remove_flag(chips, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(chips, 6, 0);
  lv_obj_set_style_pad_column(chips, 6, 0);

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macbuf[32];
  snprintf(macbuf,
           sizeof(macbuf),
           "MAC %02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],
           mac[1],
           mac[2],
           mac[3],
           mac[4],
           mac[5]);

  uint32_t up_s = (uint32_t)(esp_timer_get_time() / 1000000);
  char upbuf[32];
  snprintf(upbuf,
           sizeof(upbuf),
           "Uptime %u:%02u:%02u",
           (unsigned)(up_s / 3600),
           (unsigned)((up_s / 60) % 60),
           (unsigned)(up_s % 60));

  add_chip(chips, "ESP32-P4", true);
  add_chip(chips, macbuf, false);
  add_chip(chips, upbuf, false);
  add_chip(chips, LV_SYMBOL_COPY " 2025 HIGH CODE", true);

  lv_obj_fade_in(col, ENTRY_FADE_MS, 0);

  ui_input_set_screen_handler(about_settings_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
