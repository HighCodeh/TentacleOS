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

#include "haptic_ui.h"

#include "esp_log.h"

#include "haptic.h"
#include "led_control.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define HAPTIC_ENABLE_ROW    0
#define HAPTIC_INTENSITY_ROW 1
#define HAPTIC_MIN_PERCENT   20
#define HAPTIC_PERCENT_MAX   100

static const char *TAG = "HAPTIC_UI";
static lv_obj_t *s_screen;
static menu_component_t s_menu;

static void haptic_input(const input_event_t *event, void *context) {
  (void)context;
  if (event->action != INPUT_ACTION_PRESS && event->action != INPUT_ACTION_REPEAT)
    return;
  if (event->button == INPUT_BTN_BACK) {
    esp_err_t saved = tos_config_save(TOS_PATH_CONFIG_SYSTEM, "system");
    if (saved != ESP_OK) {
      ESP_LOGE(TAG, "Vibration settings save failed: %s", esp_err_to_name(saved));
      led_signal_error();
      return;
    }
    ESP_LOGI(TAG,
             "Vibration settings saved: enabled=%d intensity=%d",
             g_config_system.vibration,
             g_config_system.vibration_intensity);
    led_signal_info();
    ui_switch_screen(SCREEN_SETTINGS);
    return;
  }
  if (event->button == INPUT_BTN_UP)
    menu_component_prev(&s_menu);
  else if (event->button == INPUT_BTN_DOWN)
    menu_component_next(&s_menu);
  else if (event->button == INPUT_BTN_RIGHT &&
           menu_component_get_selected(&s_menu) == HAPTIC_INTENSITY_ROW)
    menu_component_intensity_inc(&s_menu, HAPTIC_INTENSITY_ROW);
  else if (event->button == INPUT_BTN_LEFT &&
           menu_component_get_selected(&s_menu) == HAPTIC_INTENSITY_ROW)
    menu_component_intensity_dec(&s_menu, HAPTIC_INTENSITY_ROW);
  else if (event->button == INPUT_BTN_OK && event->action == INPUT_ACTION_PRESS) {
    int selected = menu_component_get_selected(&s_menu);
    if (selected == HAPTIC_ENABLE_ROW) {
      g_config_system.vibration = !g_config_system.vibration;
      menu_component_set_selector_value(
          &s_menu, HAPTIC_ENABLE_ROW, g_config_system.vibration ? "On" : "Off");
    }
  }
  int intensity = menu_component_get_intensity(&s_menu, HAPTIC_INTENSITY_ROW) * HAPTIC_PERCENT_MAX /
                  INTENSITY_BAR_STEPS;
  if (intensity < HAPTIC_MIN_PERCENT) {
    menu_component_intensity_inc(&s_menu, HAPTIC_INTENSITY_ROW);
    intensity = HAPTIC_MIN_PERCENT;
  }
  g_config_system.vibration_intensity = intensity;
  haptic_set_feedback_intensity(intensity);
}

void ui_haptic_open(void) {
  if (s_screen != NULL) {
    lv_obj_delete(s_screen);
    s_screen = NULL;
  }
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  s_menu = menu_component_create(s_screen, "Vibration", "/assets/icons/vibration.bin");
  menu_component_add_selector(&s_menu,
                              "/assets/icons/vibration.bin",
                              "Menu vibration",
                              g_config_system.vibration ? "On" : "Off");
  menu_component_add_intensity(&s_menu,
                               "/assets/icons/graphic_eq.bin",
                               "Intensity",
                               g_config_system.vibration_intensity * INTENSITY_BAR_STEPS /
                                   HAPTIC_PERCENT_MAX);
  ui_input_set_screen_handler(haptic_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  ESP_LOGI(TAG, "Menu vibration settings opened");
}
