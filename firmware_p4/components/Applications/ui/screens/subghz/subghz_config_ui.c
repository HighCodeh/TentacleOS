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

#define FIELD_FREQUENCY     0
#define FIELD_PRESET        1
#define FIELD_MODE          2
#define FIELD_BANDWIDTH     3
#define FIELD_DATA_RATE     4
#define FIELD_DEVIATION     5
#define FIELD_LNA_GAIN      6
#define FIELD_DVGA_GAIN     7
#define FIELD_RESET         8
#define MHZ                 1000000U
#define KHZ                 1000U
#define FREQUENCY_TEXT_SIZE 32
#define FREQUENCY_433_HZ    433920000U
#define FREQUENCY_868_HZ    868350000U
#define FREQUENCY_915_HZ    915000000U
#define FREQUENCY_315_HZ    315000000U
#define FREQUENCY_COUNT     (sizeof(FREQUENCIES) / sizeof(FREQUENCIES[0]))
#define PRESET_COUNT        (sizeof(PRESETS) / sizeof(PRESETS[0]))

static const char *TAG = "SUBGHZ_CONFIG";
static const uint32_t FREQUENCIES[] = {
    FREQUENCY_433_HZ, FREQUENCY_868_HZ, FREQUENCY_915_HZ, FREQUENCY_315_HZ, 0};
static const char *PRESET_NAMES[] = {"AM270", "AM650", "FM238", "FM476", "AM812", "FM952"};
static const cc1101_preset_t PRESETS[] = {CC1101_PRESET_OOK_270KHZ,
                                          CC1101_PRESET_OOK_650KHZ,
                                          CC1101_PRESET_2FSK_2KHZ,
                                          CC1101_PRESET_2FSK_47KHZ,
                                          CC1101_PRESET_OOK_800KHZ,
                                          CC1101_PRESET_2FSK_95KHZ};
static const char *LNA_GAIN_NAMES[] = {
    "Max", "-2.6", "-6.1", "-7.4", "-9.2", "-11.5", "-14.6", "-17.1"};
static const char *DVGA_GAIN_NAMES[] = {"All", "Top -1", "Top -2", "Top -3"};
static lv_obj_t *s_screen;
static menu_component_t s_menu;
static int s_frequency;
static int s_preset;
static bool s_has_changes;

static void refresh(void);
static void input(const input_event_t *event, void *ctx);

void ui_subghz_config_open(void) {
  subghz_settings_t config = subghz_settings_get();
  s_frequency = s_preset = 0;
  s_has_changes = false;
  for (size_t i = 0; i < FREQUENCY_COUNT; ++i)
    if (config.freq == FREQUENCIES[i])
      s_frequency = (int)i;
  for (size_t i = 0; i < PRESET_COUNT; ++i)
    if (config.preset == PRESETS[i])
      s_preset = (int)i;
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  s_menu = menu_component_create(s_screen, "SUB-GHZ / RADIO", "/assets/icons/tune.bin");
  menu_component_add_section(&s_menu, "READ / READ RAW");
  menu_component_add_selector(&s_menu, NULL, "Freq", "");
  menu_component_add_selector(&s_menu, NULL, "Preset", "");
  menu_component_add_selector(&s_menu, NULL, "Mode", "");
  menu_component_add_selector(&s_menu, NULL, "BW (kHz)", "");
  menu_component_add_selector(&s_menu, NULL, "Rate (kBd)", "");
  menu_component_add_selector(&s_menu, NULL, "Dev (kHz)", "");
  menu_component_add_selector(&s_menu, NULL, "LNA (dB)", "");
  menu_component_add_selector(&s_menu, NULL, "DVGA", "");
  menu_component_add_item(&s_menu, NULL, "Reset preset");
  menu_component_set_hint(&s_menu, "L/R change  BACK return");
  refresh();
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void set_numeric_value(int field, uint32_t value) {
  char text[FREQUENCY_TEXT_SIZE];
  snprintf(
      text, sizeof(text), "%lu.%03lu", (unsigned long)(value / KHZ), (unsigned long)(value % KHZ));
  menu_component_set_selector_value(&s_menu, field, text);
}

static void refresh(void) {
  char text[FREQUENCY_TEXT_SIZE];
  uint32_t hz = FREQUENCIES[s_frequency];
  if (hz == 0)
    snprintf(text, sizeof(text), "Hop bands");
  else
    snprintf(text,
             sizeof(text),
             "%lu.%03lu MHz",
             (unsigned long)(hz / MHZ),
             (unsigned long)(hz % MHZ / KHZ));
  menu_component_set_selector_value(&s_menu, FIELD_FREQUENCY, text);
  subghz_settings_t config = subghz_settings_get();
  snprintf(text, sizeof(text), "%s%s", PRESET_NAMES[s_preset], config.has_custom_radio ? "*" : "");
  menu_component_set_selector_value(&s_menu, FIELD_PRESET, text);
  bool is_fsk = cc1101_config_is_fsk(&config.radio);
  menu_component_set_selector_value(&s_menu, FIELD_MODE, is_fsk ? "2-FSK" : "ASK/OOK");
  set_numeric_value(FIELD_BANDWIDTH, cc1101_config_value(&config.radio, CC1101_CONFIG_BANDWIDTH));
  set_numeric_value(FIELD_DATA_RATE, cc1101_config_value(&config.radio, CC1101_CONFIG_DATA_RATE));
  if (is_fsk)
    set_numeric_value(FIELD_DEVIATION, cc1101_config_value(&config.radio, CC1101_CONFIG_DEVIATION));
  else
    menu_component_set_selector_value(&s_menu, FIELD_DEVIATION, "N/A");
  menu_component_set_selector_value(
      &s_menu,
      FIELD_LNA_GAIN,
      LNA_GAIN_NAMES[cc1101_config_value(&config.radio, CC1101_CONFIG_LNA_GAIN)]);
  menu_component_set_selector_value(
      &s_menu,
      FIELD_DVGA_GAIN,
      DVGA_GAIN_NAMES[cc1101_config_value(&config.radio, CC1101_CONFIG_DVGA_GAIN)]);
  const char *hint = "L/R edit  * custom  BACK";
  int selected = menu_component_get_selected(&s_menu);
  if (selected == FIELD_MODE)
    hint = "Mode follows the preset";
  else if (selected == FIELD_DEVIATION && !is_fsk)
    hint = "Deviation is for FSK only";
  else if (selected == FIELD_LNA_GAIN)
    hint = "Limit below max; AGC on";
  else if (selected == FIELD_DVGA_GAIN)
    hint = "Exclude highest gain steps";
  else if (selected == FIELD_RESET)
    hint = "OK restore preset values";
  menu_component_set_hint(&s_menu, hint);
}

static void input(const input_event_t *event, void *ctx) {
  (void)ctx;
  bool press = event->action == INPUT_ACTION_PRESS;
  bool nav = press || event->action == INPUT_ACTION_REPEAT;
  if (press && event->button == INPUT_BTN_BACK) {
    if (s_has_changes) {
      subghz_settings_t config = subghz_settings_get();
      ESP_LOGI(
          TAG, "RX settings ready: hz=%lu preset=%u", (unsigned long)config.freq, config.preset);
      led_signal_info();
    }
    ui_switch_screen(SCREEN_SUBGHZ_MENU);
    return;
  }
  if (nav && event->button == INPUT_BTN_UP)
    menu_component_prev(&s_menu);
  if (nav && event->button == INPUT_BTN_DOWN)
    menu_component_next(&s_menu);
  int selected = menu_component_get_selected(&s_menu);
  if (nav && (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN)) {
    refresh();
    return;
  }
  if (press && event->button == INPUT_BTN_OK && selected == FIELD_RESET) {
    subghz_settings_set_preset(PRESETS[s_preset]);
    s_has_changes = true;
    refresh();
    return;
  }
  if ((nav && (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT)) ||
      (press && event->button == INPUT_BTN_OK)) {
    int direction = event->button == INPUT_BTN_LEFT ? -1 : 1;
    if (selected == FIELD_FREQUENCY) {
      s_frequency = (s_frequency + (int)FREQUENCY_COUNT + direction) % (int)FREQUENCY_COUNT;
      subghz_settings_set_freq(FREQUENCIES[s_frequency]);
    } else if (selected == FIELD_PRESET) {
      s_preset = (s_preset + (int)PRESET_COUNT + direction) % (int)PRESET_COUNT;
      subghz_settings_set_preset(PRESETS[s_preset]);
    } else if (selected >= FIELD_BANDWIDTH && selected <= FIELD_DVGA_GAIN) {
      subghz_settings_t config = subghz_settings_get();
      cc1101_config_field_t field = (cc1101_config_field_t)(selected - FIELD_BANDWIDTH);
      esp_err_t error = cc1101_config_step(&config.radio, field, direction);
      if (error == ESP_ERR_NOT_SUPPORTED)
        return;
      if (error == ESP_OK)
        error = subghz_settings_set_radio(&config.radio);
      if (error != ESP_OK) {
        ESP_LOGE(TAG, "Cannot update radio settings: %s", esp_err_to_name(error));
        led_signal_error();
        notify(NOTIFY_WARNING, "Cannot update radio settings");
        return;
      }
    } else {
      return;
    }
    s_has_changes = true;
    refresh();
  }
}
