// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "buttons_gpio.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "BUTTONS_GPIO";

#define BUTTON_PRESSED_LEVEL 0

static const char *const s_button_names[] = {"UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK"};

static button_t s_buttons[] = {
    {GPIO_BTN_UP_PIN, true, false},
    {GPIO_BTN_DOWN_PIN, true, false},
    {GPIO_BTN_LEFT_PIN, true, false},
    {GPIO_BTN_RIGHT_PIN, true, false},
    {GPIO_BTN_OK_PIN, true, false},
    {GPIO_BTN_BACK_PIN, true, false},
};

#define NUM_BUTTONS (sizeof(s_buttons) / sizeof(s_buttons[0]))

static bool s_last_down[6] = {false};

static bool get_raw_level(uint32_t gpio) {
  return gpio_get_level(gpio) == BUTTON_PRESSED_LEVEL;
}

static bool check_and_log(int idx, uint32_t gpio) {
  bool now = gpio_get_level(gpio) == BUTTON_PRESSED_LEVEL;
  if (now && !s_last_down[idx]) {
    ESP_LOGI(TAG, "Pressed: %s (GPIO %lu)", s_button_names[idx], (unsigned long)gpio);
  }
  s_last_down[idx] = now;
  return now;
}

bool up_button_pressed(void) {
  return s_buttons[0].pressed_flag &&
         (__atomic_test_and_set(&s_buttons[0].pressed_flag, __ATOMIC_RELAXED), false);
}
bool down_button_pressed(void) {
  return s_buttons[1].pressed_flag &&
         (__atomic_test_and_set(&s_buttons[1].pressed_flag, __ATOMIC_RELAXED), false);
}
bool left_button_pressed(void) {
  return s_buttons[2].pressed_flag &&
         (__atomic_test_and_set(&s_buttons[2].pressed_flag, __ATOMIC_RELAXED), false);
}
bool right_button_pressed(void) {
  return s_buttons[3].pressed_flag &&
         (__atomic_test_and_set(&s_buttons[3].pressed_flag, __ATOMIC_RELAXED), false);
}
bool ok_button_pressed(void) {
  return s_buttons[4].pressed_flag &&
         (__atomic_test_and_set(&s_buttons[4].pressed_flag, __ATOMIC_RELAXED), false);
}
bool back_button_pressed(void) {
  return s_buttons[5].pressed_flag &&
         (__atomic_test_and_set(&s_buttons[5].pressed_flag, __ATOMIC_RELAXED), false);
}

bool up_button_is_down(void) {
  return check_and_log(0, GPIO_BTN_UP_PIN);
}
bool down_button_is_down(void) {
  return check_and_log(1, GPIO_BTN_DOWN_PIN);
}
bool left_button_is_down(void) {
  return check_and_log(2, GPIO_BTN_LEFT_PIN);
}
bool right_button_is_down(void) {
  return check_and_log(3, GPIO_BTN_RIGHT_PIN);
}
bool ok_button_is_down(void) {
  return check_and_log(4, GPIO_BTN_OK_PIN);
}
bool back_button_is_down(void) {
  return check_and_log(5, GPIO_BTN_BACK_PIN);
}

void buttons_task(void) {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    bool current = get_raw_level(s_buttons[i].gpio);

    if (s_buttons[i].last_state == true && current == false) {
      s_buttons[i].pressed_flag = true;
      ESP_LOGI(TAG, "Pressed: %s (GPIO %lu)", s_button_names[i], (unsigned long)s_buttons[i].gpio);
    }

    s_buttons[i].last_state = current;
  }
}
void buttons_init(void) {
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask =
          ((1ULL << GPIO_BTN_UP_PIN) | (1ULL << GPIO_BTN_DOWN_PIN) | (1ULL << GPIO_BTN_LEFT_PIN) |
           (1ULL << GPIO_BTN_RIGHT_PIN) | (1ULL << GPIO_BTN_OK_PIN) | (1ULL << GPIO_BTN_BACK_PIN)),
      .pull_down_en = 0,
      .pull_up_en = 1,
  };
  gpio_config(&io_conf);

  for (int i = 0; i < NUM_BUTTONS; i++) {
    s_buttons[i].last_state = get_raw_level(s_buttons[i].gpio);
    s_buttons[i].pressed_flag = false;
  }
}
