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
#include "esp_attr.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "led_control.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "subghz_settings.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_chrome.h"
#include "ui_theme.h"

#define FIELD_FREQUENCY        0
#define FIELD_PRESET           1
#define FIELD_MODE             2
#define FIELD_BANDWIDTH        3
#define FIELD_DATA_RATE        4
#define FIELD_DEVIATION        5
#define FIELD_LNA_GAIN         6
#define FIELD_DVGA_GAIN        7
#define FIELD_RESET            8
#define MHZ                    1000000U
#define KHZ                    1000U
#define FREQUENCY_TEXT_SIZE    32
#define DIAL_HEIGHT_PORTRAIT   126
#define DIAL_HEIGHT_LANDSCAPE  100
#define DIAL_PAD               10
#define DIAL_TITLE_Y           0
#define DIAL_VALUE_Y_PORTRAIT  34
#define DIAL_VALUE_Y_LANDSCAPE 22
#define DIAL_RULER_BOTTOM      9
#define DIAL_SCALE             384
#define DIAL_PIVOT_PERCENT     50
#define DIAL_TICK_COUNT        21
#define DIAL_TICK_WIDTH        2
#define DIAL_TICK_MINOR        6
#define DIAL_TICK_MAJOR        12
#define DIAL_TICK_CENTER       22
#define DIAL_TICK_PERIOD       5
#define DIAL_CENTER            (DIAL_TICK_COUNT / 2)
#define DIAL_ARROWS_INSET      4
#define PRESET_COUNT           (sizeof(PRESETS) / sizeof(PRESETS[0]))

static const char *TAG = "SUBGHZ_CONFIG";
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
EXT_RAM_BSS_ATTR static menu_component_t s_menu;
static lv_obj_t *s_frequency_value;
static lv_obj_t *s_frequency_unit;
static lv_obj_t *s_frequency_ruler;
static lv_obj_t *s_frequency_ticks[DIAL_TICK_COUNT];
static size_t s_frequency_index;
static int s_preset;
static bool s_has_changes;

static void refresh(void);
static void create_frequency_dial(void);
static void refresh_frequency_dial(void);
static void input(const input_event_t *event, void *ctx);

void ui_subghz_config_open(void) {
  subghz_settings_t config = subghz_settings_get();
  s_preset = 0;
  s_has_changes = false;
  s_frequency_index = 0;
  size_t count;
  const uint32_t *frequencies = subghz_settings_get_frequencies(&count);
  for (size_t i = 0; i < count; ++i) {
    if (frequencies[i] == config.freq) {
      s_frequency_index = i;
      break;
    }
  }
  for (size_t i = 0; i < PRESET_COUNT; ++i)
    if (config.preset == PRESETS[i])
      s_preset = (int)i;
  s_screen = lv_obj_create(NULL);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  s_menu = menu_component_create(s_screen, "SUB-GHZ / RADIO", "/assets/icons/tune.bin");
  create_frequency_dial();
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
  refresh_frequency_dial();
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
  if (selected == FIELD_FREQUENCY)
    hint = "L/R tune   UP/DOWN field";
  else if (selected == FIELD_MODE)
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
      if (event->button == INPUT_BTN_OK)
        return;
      size_t count;
      const uint32_t *frequencies = subghz_settings_get_frequencies(&count);
      s_frequency_index =
          direction < 0 ? (s_frequency_index + count - 1) % count : (s_frequency_index + 1) % count;
      subghz_settings_set_freq(frequencies[s_frequency_index]);
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

static void create_frequency_dial(void) {
  lv_obj_t *card = menu_component_add_item(&s_menu, NULL, "");
  lv_obj_clean(card);
  s_menu.sel_dots[FIELD_FREQUENCY] = NULL;
  lv_obj_set_layout(card, LV_LAYOUT_NONE);
  lv_obj_set_style_pad_all(card, DIAL_PAD, 0);
  lv_obj_set_height(card,
                    ui_screen_h() > ui_screen_w() ? DIAL_HEIGHT_PORTRAIT : DIAL_HEIGHT_LANDSCAPE);
  lv_obj_set_style_bg_image_src(card, NULL, 0);
  ui_chrome_gradient(card,
                     current_theme.bg_item_top,
                     current_theme.bg_item_bot,
                     ui_screen_h() > ui_screen_w() ? DIAL_HEIGHT_PORTRAIT : DIAL_HEIGHT_LANDSCAPE);
  int32_t value_y = ui_screen_h() > ui_screen_w() ? DIAL_VALUE_Y_PORTRAIT : DIAL_VALUE_Y_LANDSCAPE;
  lv_obj_t *title = lv_label_create(card);
  s_frequency_unit = title;
  lv_label_set_text(title, "FREQUENCY");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, DIAL_TITLE_Y);
  s_frequency_value = lv_label_create(card);
  lv_obj_set_style_text_font(s_frequency_value, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(s_frequency_value, current_theme.text_main, 0);
  lv_obj_set_style_transform_scale(s_frequency_value, DIAL_SCALE, 0);
  lv_obj_set_style_transform_pivot_x(s_frequency_value, lv_pct(DIAL_PIVOT_PERCENT), 0);
  lv_obj_set_style_transform_pivot_y(s_frequency_value, lv_pct(DIAL_PIVOT_PERCENT), 0);
  lv_obj_align(s_frequency_value, LV_ALIGN_TOP_MID, 0, value_y);
  s_menu.val_labels[FIELD_FREQUENCY] = s_frequency_value;
  lv_obj_t *left = lv_label_create(card);
  lv_label_set_text(left, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(left, current_theme.border_accent, 0);
  lv_obj_align(left, LV_ALIGN_TOP_LEFT, DIAL_ARROWS_INSET, value_y);
  lv_obj_t *right = lv_label_create(card);
  lv_label_set_text(right, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(right, current_theme.border_accent, 0);
  lv_obj_align(right, LV_ALIGN_TOP_RIGHT, -DIAL_ARROWS_INSET, value_y);
  s_frequency_ruler = lv_obj_create(card);
  lv_obj_remove_style_all(s_frequency_ruler);
  lv_obj_set_size(s_frequency_ruler, lv_pct(100), DIAL_TICK_CENTER);
  lv_obj_align(s_frequency_ruler, LV_ALIGN_BOTTOM_MID, 0, -DIAL_RULER_BOTTOM);
  lv_obj_remove_flag(s_frequency_ruler, LV_OBJ_FLAG_SCROLLABLE);
  for (size_t i = 0; i < DIAL_TICK_COUNT; ++i) {
    lv_obj_t *tick = lv_obj_create(s_frequency_ruler);
    lv_obj_remove_style_all(tick);
    lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tick, 1, 0);
    s_frequency_ticks[i] = tick;
  }
}

static void refresh_frequency_dial(void) {
  uint32_t hz = subghz_settings_get().freq;
  char text[FREQUENCY_TEXT_SIZE];
  if (hz == 0) {
    snprintf(text, sizeof(text), "Hop bands");
  } else {
    snprintf(text, sizeof(text), "%lu.%06lu", (unsigned long)(hz / MHZ), (unsigned long)(hz % MHZ));
    size_t length = strlen(text);
    while (length > 0 && text[length - 1] == '0')
      text[--length] = '\0';
    if (length > 0 && text[length - 1] == '.')
      text[--length] = '\0';
  }
  lv_label_set_text(s_frequency_value, text);
  lv_label_set_text(s_frequency_unit, hz == 0 ? "FREQUENCY / AUTO" : "FREQUENCY / MHz");
  lv_obj_update_layout(s_frequency_ruler);
  int32_t width = lv_obj_get_width(s_frequency_ruler) - DIAL_TICK_WIDTH;
  for (size_t i = 0; i < DIAL_TICK_COUNT; ++i) {
    bool is_center = i == DIAL_CENTER;
    bool is_major = (i + s_frequency_index) % DIAL_TICK_PERIOD == 0;
    int32_t height = is_center ? DIAL_TICK_CENTER : is_major ? DIAL_TICK_MAJOR : DIAL_TICK_MINOR;
    lv_obj_set_size(s_frequency_ticks[i], DIAL_TICK_WIDTH, height);
    lv_obj_set_pos(s_frequency_ticks[i],
                   (int32_t)i * width / (DIAL_TICK_COUNT - 1),
                   DIAL_TICK_CENTER - height);
    lv_obj_set_style_bg_color(
        s_frequency_ticks[i], is_center ? current_theme.border_accent : current_theme.text_main, 0);
    lv_obj_set_style_bg_opa(s_frequency_ticks[i], is_center ? LV_OPA_COVER : LV_OPA_50, 0);
  }
}
