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
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define HAPTIC_INTENSITY_ROW 0
#define HAPTIC_PLAY_ROW      3
#define HAPTIC_PULSE_MS      200
#define HAPTIC_INITIAL_LEVEL 3
#define HAPTIC_PERCENT_MAX   100
#define HAPTIC_REFRESH_MS    100
#define HAPTIC_PROBE_ROW     1
#define HAPTIC_STOP_ROW      2

static const char *TAG = "HAPTIC_UI";
static lv_obj_t *s_screen;
static menu_component_t s_menu;
static lv_timer_t *s_timer;

static void refresh(lv_timer_t *timer) {
  (void)timer;
  const char *text;
  if (haptic_is_busy())
    text = "Working...";
  else {
    esp_err_t result = haptic_last_result();
    text = result == ESP_ERR_NOT_FINISHED ? "Not checked"
           : result == ESP_OK             ? "Done; stopped"
                                          : esp_err_to_name(result);
  }
  menu_component_set_selector_value(&s_menu, HAPTIC_PROBE_ROW, text);
}

static void deleted(lv_event_t *event) {
  (void)event;
  haptic_stop();
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
}

static void haptic_input(const input_event_t *event, void *context) {
  (void)context;
  if (event->action != INPUT_ACTION_PRESS && event->action != INPUT_ACTION_REPEAT)
    return;
  if (event->button == INPUT_BTN_BACK) {
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
    esp_err_t result = ESP_OK;
    if (selected == HAPTIC_STOP_ROW)
      haptic_stop();
    else if (selected == HAPTIC_PROBE_ROW)
      result = haptic_probe();
    else if (selected == HAPTIC_PLAY_ROW || selected == HAPTIC_INTENSITY_ROW) {
      uint8_t intensity = menu_component_get_intensity(&s_menu, HAPTIC_INTENSITY_ROW) *
                          HAPTIC_PERCENT_MAX / INTENSITY_BAR_STEPS;
      if (intensity != 0)
        result = haptic_play(intensity, HAPTIC_PULSE_MS);
    }
    if (result != ESP_OK)
      ESP_LOGW(TAG, "Request rejected: %s", esp_err_to_name(result));
  }
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
  menu_component_add_intensity(
      &s_menu, "/assets/icons/graphic_eq.bin", "Intensity", HAPTIC_INITIAL_LEVEL);
  menu_component_add_selector(&s_menu, "/assets/icons/tune.bin", "Check driver", "Not checked");
  menu_component_add_item(&s_menu, "/assets/icons/vibration.bin", "Stop / hold reset");
  menu_component_add_item(&s_menu, "/assets/icons/vibration.bin", "Play pulse (200 ms)");
  s_timer = lv_timer_create(refresh, HAPTIC_REFRESH_MS, NULL);
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  ui_input_set_screen_handler(haptic_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  ESP_LOGI(TAG, "AW8623 haptics opened; ERM 3.0 V design reference");
}
