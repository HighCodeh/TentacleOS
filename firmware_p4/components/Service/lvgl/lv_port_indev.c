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

#include "lv_port_indev.h"

#include "lvgl.h"
#include "core/lv_group.h"
#include "esp_log.h"

#include "buttons_gpio.h"
#include "lvgl_glue.h"

static const char *TAG = "LV_PORT_INDEV";

lv_indev_t *indev_keypad = NULL;
lv_group_t *main_group = NULL;

// Remote key injected by the screen-share module (companion app control buttons).
// Played out as one PRESS then one RELEASE across two reads so LVGL registers a
// single tap. Physical buttons take priority. phase: 0 idle, 1 press, 2 release.
static volatile uint32_t s_remote_key = 0;
static volatile int s_remote_phase = 0;

// When true, the keypad reports "no button" so the LVGL group (menus/lists/home)
// receives no keys. The global dropdown raises this while open so it owns input.
static volatile bool s_suppressed = false;

void lv_port_indev_set_suppressed(bool suppressed) {
  s_suppressed = suppressed;
}

static void keypad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  static uint32_t last_key = 0;
  uint32_t key = 0;

  // Held down by the global dropdown: swallow all keys so the screen underneath
  // does not navigate while the panel is open.
  if (s_suppressed) {
    data->key = last_key;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  const bool landscape = lvgl_glue_is_landscape();

  if (up_button_is_down()) {
    key = landscape ? LV_KEY_LEFT : LV_KEY_UP;
  } else if (down_button_is_down()) {
    key = landscape ? LV_KEY_RIGHT : LV_KEY_DOWN;
  } else if (left_button_is_down()) {
    key = landscape ? LV_KEY_DOWN : LV_KEY_LEFT;
  } else if (right_button_is_down()) {
    key = landscape ? LV_KEY_UP : LV_KEY_RIGHT;
  } else if (ok_button_is_down()) {
    key = LV_KEY_ENTER;
  } else if (back_button_is_down()) {
    key = LV_KEY_ESC;
  }

  // No physical button: play out an injected remote key, if any.
  if (key == 0 && s_remote_phase != 0) {
    last_key = s_remote_key;
    data->key = s_remote_key;
    if (s_remote_phase == 1) {
      data->state = LV_INDEV_STATE_PRESSED;
      s_remote_phase = 2;
    } else {
      data->state = LV_INDEV_STATE_RELEASED;
      s_remote_phase = 0;
    }
    return;
  }

  if (key != 0) {
    last_key = key;
    data->key = key;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->key = last_key;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void lv_port_indev_inject(uint32_t lv_key) {
  s_remote_key = lv_key;
  s_remote_phase = 1;
}

void lv_port_indev_init(void) {
  if (main_group != NULL) {
    return;
  }

  if (!lvgl_glue_lock(-1)) {
    ESP_LOGE(TAG, "could not lock LVGL to create the input group");
    return;
  }

  main_group = lv_group_create();
  lv_group_set_default(main_group);

  indev_keypad = lv_indev_create();
  lv_indev_set_type(indev_keypad, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(indev_keypad, keypad_read);
  lv_indev_set_group(indev_keypad, main_group);

  lvgl_glue_unlock();
  ESP_LOGI(TAG, "main_group + keypad indev ready");
}

void lv_port_indev_set_keyboard_mode(bool enable) {
  (void)enable;
}
