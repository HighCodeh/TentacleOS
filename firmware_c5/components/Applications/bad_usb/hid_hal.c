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


#include "hid_hal.h"
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <rom/ets_sys.h>

static hid_send_callback_t s_send_cb = NULL;
static hid_mouse_callback_t s_mouse_cb = NULL;
static hid_wait_callback_t s_wait_cb = NULL;

void hid_hal_register_callback(hid_send_callback_t send_cb, hid_mouse_callback_t mouse_cb, hid_wait_callback_t wait_cb) {
  s_send_cb = send_cb;
  s_mouse_cb = mouse_cb;
  s_wait_cb = wait_cb;
}

void hid_hal_press_key(uint8_t keycode, uint8_t modifiers) {
  if (s_send_cb) {
    s_send_cb(keycode, modifiers);
    ets_delay_us(5000); // 5ms delay

    s_send_cb(0, 0);
    ets_delay_us(5000); // 5ms delay
    
    vTaskDelay(0); // Yield to prevent WDT starvation
  }
}

void hid_hal_mouse_move(int8_t x, int8_t y) {
    if (s_mouse_cb) {
        s_mouse_cb(x, y, 0, 0);
        ets_delay_us(2000);
        vTaskDelay(0);
    }
}

void hid_hal_mouse_click(uint8_t buttons) {
    if (s_mouse_cb) {
        s_mouse_cb(0, 0, buttons, 0); // Press
        ets_delay_us(5000);
        s_mouse_cb(0, 0, 0, 0);       // Release
        ets_delay_us(5000);
        vTaskDelay(0);
    }
}

void hid_hal_mouse_scroll(int8_t wheel) {
    if (s_mouse_cb) {
        s_mouse_cb(0, 0, 0, wheel);
        ets_delay_us(2000);
        vTaskDelay(0);
    }
}

void hid_hal_wait_for_connection(void) {
  if (s_wait_cb) {
    s_wait_cb();
  }
}
