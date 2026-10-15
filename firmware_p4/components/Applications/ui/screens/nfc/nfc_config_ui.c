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

#include "nfc_config_ui.h"

#include <stdio.h>

#include "lvgl.h"

#include "highboy_nfc.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "nfc_poller.h"
#include "notify_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

enum { CFG_FIELD, CFG_POLL, CFG_AAT, CFG_DIAG, CFG_COUNT };
static const char *const POLL_NAMES[] = {"Slow", "Normal", "Fast"};
#define POLL_N 3

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static int s_poll = 1;
static bool s_field_on;

static esp_err_t ensure_hardware(void) {
  if (highboy_nfc_is_initialized())
    return ESP_OK;
  highboy_nfc_config_t config = HIGHBOY_NFC_CONFIG_DEFAULT();
  return highboy_nfc_init(&config);
}

static void apply_poll_timing(void) {
  static const uint32_t GUARD_US[POLL_N] = {5000, 1000, 0};
  nfc_poller_set_timing(GUARD_US[s_poll], 0, false);
}

static void set_field(bool on) {
  esp_err_t err = ensure_hardware();
  if (err == ESP_OK)
    err = on ? highboy_nfc_field_on() : highboy_nfc_field_off();
  if (err == ESP_OK) {
    s_field_on = on;
    menu_component_set_toggle(&s_menu, CFG_FIELD, on);
    notify(NOTIFY_SAVED, on ? "13.56 MHz RF field on" : "NFC RF field off");
  } else {
    menu_component_set_toggle(&s_menu, CFG_FIELD, s_field_on);
    notify(NOTIFY_WARNING, "NFC hardware did not respond");
  }
}

static void run_regulator_adjust(void) {
  esp_err_t err = ensure_hardware();
  if (err == ESP_OK)
    err = highboy_nfc_adjust_regulator_auto();
  if (err == ESP_OK)
    notify(NOTIFY_SAVED, "RF regulator auto-adjusted");
  else
    notify(NOTIFY_WARNING, "RF regulator adjustment failed");
}

static void show_diagnostic(void) {
  char message[160];
  uint8_t id = 0;
  uint16_t regulator_mv = 0;
  bool current_limit = false;
  bool manual = false;
  esp_err_t err = ensure_hardware();
  if (err == ESP_OK)
    err = highboy_nfc_ping(&id);
  if (err != ESP_OK) {
    snprintf(message, sizeof(message), "ST25R3916 unavailable\nerror 0x%X", (unsigned)err);
  } else {
    const bool temporary_field = !s_field_on;
    if (temporary_field)
      (void)highboy_nfc_field_on();
    const uint8_t amplitude = highboy_nfc_measure_amplitude();
    if (temporary_field)
      (void)highboy_nfc_field_off();
    esp_err_t regulator_err =
        highboy_nfc_get_regulator(&regulator_mv, &current_limit, &manual, NULL);
    snprintf(message,
             sizeof(message),
             "ST25R3916 online\nID 0x%02X   amplitude %u\nregulator %s%u mV\ncurrent limit %s",
             id,
             amplitude,
             regulator_err == ESP_OK ? "" : "n/a ",
             regulator_err == ESP_OK ? regulator_mv : 0,
             current_limit ? "yes" : "no");
  }
  msgbox_open(err == ESP_OK ? LV_SYMBOL_OK : LV_SYMBOL_WARNING, message, NULL, NULL, NULL);
}

static void nfc_config_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  int sel = menu_component_get_selected(&s_menu);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_NFC_MENU);
      break;
    case INPUT_BTN_LEFT:
      if (press) {
        if (sel == CFG_POLL) {
          s_poll = (s_poll - 1 + POLL_N) % POLL_N;
          menu_component_set_selector_value(&s_menu, CFG_POLL, POLL_NAMES[s_poll]);
          apply_poll_timing();
        } else {
          ui_switch_screen(SCREEN_NFC_MENU);
        }
      }
      break;
    case INPUT_BTN_RIGHT:
      if (press && sel == CFG_POLL) {
        s_poll = (s_poll + 1) % POLL_N;
        menu_component_set_selector_value(&s_menu, CFG_POLL, POLL_NAMES[s_poll]);
        apply_poll_timing();
      }
      break;
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_OK:
      if (press) {
        if (sel == CFG_FIELD) {
          set_field(!s_field_on);
        } else if (sel == CFG_AAT) {
          run_regulator_adjust();
        } else if (sel == CFG_DIAG) {
          show_diagnostic();
        }
      }
      break;
    default:
      break;
  }
}

void ui_nfc_config_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "NFC Config", "/assets/icons/settings.bin");
  menu_component_add_toggle(
      &s_menu, "/assets/icons/power_settings_new.bin", "RF field (13.56 MHz)", s_field_on);
  menu_component_add_selector(
      &s_menu, "/assets/icons/autorenew.bin", "Poll rate", POLL_NAMES[s_poll]);
  menu_component_add_item(&s_menu, "/assets/icons/settings_input_antenna.bin", "RF regulator auto");
  menu_component_add_item(&s_menu, "/assets/icons/troubleshoot.bin", "Bus Diagnostic");

  ui_input_set_screen_handler(nfc_config_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
