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

static void keypad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  static uint32_t last_key = 0;
  uint32_t key = 0;

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

  if (key != 0) {
    last_key = key;
    data->key = key;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->key = last_key;
    data->state = LV_INDEV_STATE_RELEASED;
  }
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
