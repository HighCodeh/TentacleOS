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

#include "subghz_config_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "led_control.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "subghz_settings.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define FREQUENCY_COUNT 5
#define PRESET_COUNT    6
#define FIELD_FREQUENCY 0
#define FIELD_PRESET    1
#define FIELD_APPLY     2
#define MHZ             1000000U
static const char *TAG = "SUBGHZ_CONFIG";
static const uint32_t FREQUENCIES[] = {315000000, 433920000, 868350000, 915000000, 0};
static const char *PRESET_NAMES[] = {
    "OOK 270 kHz", "OOK 650 kHz", "OOK 812 kHz", "FSK 2.38 kHz", "FSK 47.6 kHz", "FSK 95.2 kHz"};
static const cc1101_preset_t PRESETS[] = {CC1101_PRESET_OOK_270KHZ,
                                          CC1101_PRESET_OOK_650KHZ,
                                          CC1101_PRESET_OOK_800KHZ,
                                          CC1101_PRESET_2FSK_2KHZ,
                                          CC1101_PRESET_2FSK_47KHZ,
                                          CC1101_PRESET_2FSK_95KHZ};
static lv_obj_t *s_screen;
static menu_component_t s_menu;
static int s_frequency;
static int s_preset;
static void refresh(void) {
  char text[32];
  uint32_t hz = FREQUENCIES[s_frequency];
  if (hz == 0)
    snprintf(text, sizeof(text), "Hop bands");
  else
    snprintf(text,
             sizeof(text),
             "%lu.%03lu MHz",
             (unsigned long)(hz / MHZ),
             (unsigned long)(hz % MHZ / 1000));
  menu_component_set_selector_value(&s_menu, FIELD_FREQUENCY, text);
  menu_component_set_selector_value(&s_menu, FIELD_PRESET, PRESET_NAMES[s_preset]);
}
static void input(const input_event_t *event, void *ctx) {
  (void)ctx;
  bool press = event->action == INPUT_ACTION_PRESS;
  bool nav = press || event->action == INPUT_ACTION_REPEAT;
  if (press && event->button == INPUT_BTN_BACK) {
    ui_switch_screen(SCREEN_SUBGHZ_MENU);
    return;
  }
  if (nav && event->button == INPUT_BTN_UP)
    menu_component_prev(&s_menu);
  if (nav && event->button == INPUT_BTN_DOWN)
    menu_component_next(&s_menu);
  int selected = menu_component_get_selected(&s_menu);
  if (press && event->button == INPUT_BTN_OK && selected == FIELD_APPLY) {
    subghz_settings_set_freq(FREQUENCIES[s_frequency]);
    subghz_settings_set_preset(PRESETS[s_preset]);
    ESP_LOGI(TAG,
             "RX settings: hz=%lu preset=%u",
             (unsigned long)FREQUENCIES[s_frequency],
             PRESETS[s_preset]);
    led_signal_info();
    notify(NOTIFY_SAVED, "Ready for Read / Read RAW");
    return;
  }
  if ((nav && (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT)) ||
      (press && event->button == INPUT_BTN_OK)) {
    int direction = event->button == INPUT_BTN_LEFT ? -1 : 1;
    if (selected == FIELD_FREQUENCY)
      s_frequency = (s_frequency + FREQUENCY_COUNT + direction) % FREQUENCY_COUNT;
    if (selected == FIELD_PRESET)
      s_preset = (s_preset + PRESET_COUNT + direction) % PRESET_COUNT;
    refresh();
  }
}
void ui_subghz_config_open(void) {
  subghz_settings_t config = subghz_settings_get();
  s_frequency = s_preset = 0;
  for (int i = 0; i < FREQUENCY_COUNT; ++i)
    if (config.freq == FREQUENCIES[i])
      s_frequency = i;
  for (int i = 0; i < PRESET_COUNT; ++i)
    if (config.preset == PRESETS[i])
      s_preset = i;
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  s_menu = menu_component_create(s_screen, "SUB-GHZ / RADIO", "/assets/icons/tune.bin");
  menu_component_add_section(&s_menu, "READ / READ RAW");
  menu_component_add_selector(&s_menu, NULL, "Freq", "");
  menu_component_add_selector(&s_menu, NULL, "Preset", "");
  menu_component_add_item(&s_menu, "/assets/icons/tune.bin", "Apply RX settings");
  menu_component_add_section(&s_menu, "TX +10 dBm nominal");
  menu_component_add_section(&s_menu, "315: RF1 / auto RX gain");
  menu_component_set_hint(&s_menu, "UP/DN select  L/R edit  OK apply");
  refresh();
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}
