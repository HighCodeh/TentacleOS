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


#ifndef HID_HAL_H
#define HID_HAL_H

#include <stdint.h>

typedef void (*hid_send_callback_t)(uint8_t keycode, uint8_t modifiers);
typedef void (*hid_mouse_callback_t)(int8_t x, int8_t y, uint8_t buttons, int8_t wheel);
typedef void (*hid_wait_callback_t)(void);

void hid_hal_register_callback(hid_send_callback_t send_cb, hid_mouse_callback_t mouse_cb, hid_wait_callback_t wait_cb);
void hid_hal_press_key(uint8_t keycode, uint8_t modifiers);
void hid_hal_mouse_move(int8_t x, int8_t y);
void hid_hal_mouse_click(uint8_t buttons);
void hid_hal_mouse_scroll(int8_t wheel);
void hid_hal_wait_for_connection(void);

#endif
