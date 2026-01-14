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


#include "hid_layouts.h"
#include "hid_hal.h"
#include "class/hid/hid_device.h"
#include <stdbool.h>

#ifndef HID_KEY_INTERNATIONAL_1
#define HID_KEY_INTERNATIONAL_1 0x87
#endif

void type_string_us(const char* str) {
  for (size_t i = 0; str[i] != '\0'; ++i) {
    char c = str[i];
    uint8_t keycode = 0;
    uint8_t modifier = 0;
    if (c >= 'a' && c <= 'z') { keycode = HID_KEY_A + (c - 'a'); }
    else if (c >= 'A' && c <= 'Z') { keycode = HID_KEY_A + (c - 'A'); modifier = KEYBOARD_MODIFIER_LEFTSHIFT; }
    else if (c >= '1' && c <= '9') { keycode = HID_KEY_1 + (c - '1'); }
    else if (c == '0') { keycode = HID_KEY_0; }
    else {
      switch (c) {
        case ' ': keycode = HID_KEY_SPACE; break;
        case '!': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_1; break;
        case '@': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_2; break;
        case '#': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_3; break;
        case '$': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_4; break;
        case '%': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_5; break;
        case '^': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_6; break;
        case '&': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_7; break;
        case '*': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_8; break;
        case '(': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_9; break;
        case ')': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_0; break;
        case '-': keycode = HID_KEY_MINUS; break;
        case '_': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_MINUS; break;
        case '=': keycode = HID_KEY_EQUAL; break;
        case '+': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_EQUAL; break;
        case '.': keycode = HID_KEY_PERIOD; break;
        case ',': keycode = HID_KEY_COMMA; break;
        case '/': keycode = HID_KEY_SLASH; break;
        case '?': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_SLASH; break;
        case ':': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_SEMICOLON; break;
        case ';': keycode = HID_KEY_SEMICOLON; break;
      }
    }
    if (keycode != 0) hid_hal_press_key(keycode, modifier);
  }
}

void type_string_abnt2(const char* str) {
  for (size_t i = 0; str[i] != '\0'; ++i) {
    uint8_t c1 = (uint8_t)str[i];
    uint8_t c2 = (uint8_t)str[i+1];

    if (c1 == '\'') {
      hid_hal_press_key(HID_KEY_BRACKET_LEFT, 0);
      hid_hal_press_key(HID_KEY_SPACE, 0);
      continue; 
    }
    if (c1 == '"') { 
      hid_hal_press_key(HID_KEY_BRACKET_LEFT, KEYBOARD_MODIFIER_LEFTSHIFT);
      continue; 
    }
    if ((c1 & 0xE0) == 0xC0 && c2 != '\0') {
      bool char_processed = true;
      if (c1 == 0xC3 && c2 == 0xA7) { hid_hal_press_key(HID_KEY_SEMICOLON, 0); }
      else if (c1 == 0xC3 && c2 == 0x87) { hid_hal_press_key(HID_KEY_SEMICOLON, KEYBOARD_MODIFIER_LEFTSHIFT); }
      else if (c1 == 0xC3 && c2 == 0xA1) { hid_hal_press_key(HID_KEY_BRACKET_LEFT, 0); hid_hal_press_key(HID_KEY_A, 0); }
      else if (c1 == 0xC3 && c2 == 0xA9) { hid_hal_press_key(HID_KEY_BRACKET_LEFT, 0); hid_hal_press_key(HID_KEY_E, 0); }
      else if (c1 == 0xC3 && c2 == 0xAD) { hid_hal_press_key(HID_KEY_BRACKET_LEFT, 0); hid_hal_press_key(HID_KEY_I, 0); }
      else if (c1 == 0xC3 && c2 == 0xB3) { hid_hal_press_key(HID_KEY_BRACKET_LEFT, 0); hid_hal_press_key(HID_KEY_O, 0); }
      else if (c1 == 0xC3 && c2 == 0xBA) { hid_hal_press_key(HID_KEY_BRACKET_LEFT, 0); hid_hal_press_key(HID_KEY_U, 0); }
      else if (c1 == 0xC3 && c2 == 0xA2) { hid_hal_press_key(HID_KEY_APOSTROPHE, KEYBOARD_MODIFIER_LEFTSHIFT); hid_hal_press_key(HID_KEY_A, 0); }
      else if (c1 == 0xC3 && c2 == 0xAA) { hid_hal_press_key(HID_KEY_APOSTROPHE, KEYBOARD_MODIFIER_LEFTSHIFT); hid_hal_press_key(HID_KEY_E, 0); }
      else if (c1 == 0xC3 && c2 == 0xB4) { hid_hal_press_key(HID_KEY_APOSTROPHE, KEYBOARD_MODIFIER_LEFTSHIFT); hid_hal_press_key(HID_KEY_O, 0); }
      else if (c1 == 0xC3 && c2 == 0xA3) { hid_hal_press_key(HID_KEY_APOSTROPHE, 0); hid_hal_press_key(HID_KEY_A, 0); }
      else if (c1 == 0xC3 && c2 == 0xB5) { hid_hal_press_key(HID_KEY_APOSTROPHE, 0); hid_hal_press_key(HID_KEY_O, 0); }
      else if (c1 == 0xC3 && c2 == 0xA0) { hid_hal_press_key(HID_KEY_BRACKET_LEFT, KEYBOARD_MODIFIER_LEFTSHIFT); hid_hal_press_key(HID_KEY_A, 0); }
      else { char_processed = false; }

      if (char_processed) { i++; continue; }
    }

    uint8_t keycode = 0;
    uint8_t modifier = 0;

    if (c1 >= 'a' && c1 <= 'z') { keycode = HID_KEY_A + (c1 - 'a'); }
    else if (c1 >= 'A' && c1 <= 'Z') { modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_A + (c1 - 'A'); }
    else if (c1 >= '1' && c1 <= '9') { keycode = HID_KEY_1 + (c1 - '1'); }
    else if (c1 == '0') { keycode = HID_KEY_0; }
    else {
      switch (c1) {
        case '!': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_1; break;
        case '@': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_2; break;
        case '#': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_3; break;
        case '$': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_4; break;
        case '%': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_5; break;
        case '&': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_7; break;
        case '*': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_8; break;
        case '(': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_9; break;
        case ')': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_0; break;
        case ' ': keycode = HID_KEY_SPACE; break;
        case '\n': keycode = HID_KEY_ENTER; break;
        case '\t': keycode = HID_KEY_TAB; break;
        case '-': keycode = HID_KEY_MINUS; break;
        case '=': keycode = HID_KEY_EQUAL; break;
        case '_': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_MINUS; break;
        case '+': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_EQUAL; break;
        case '.': keycode = HID_KEY_PERIOD; break;
        case ',': keycode = HID_KEY_COMMA; break;
        case ';': keycode = HID_KEY_SLASH; break;
        case ':': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_SLASH; break;
        case '/': keycode = HID_KEY_INTERNATIONAL_1; break;
        case '?': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_INTERNATIONAL_1; break;
        case '[': modifier = KEYBOARD_MODIFIER_RIGHTALT; keycode = HID_KEY_BRACKET_LEFT; break;
        case '{': modifier = KEYBOARD_MODIFIER_RIGHTALT; keycode = HID_KEY_BRACKET_LEFT; modifier |= KEYBOARD_MODIFIER_LEFTSHIFT; break;
        case ']': modifier = KEYBOARD_MODIFIER_RIGHTALT; keycode = HID_KEY_BRACKET_RIGHT; break;
        case '}': modifier = KEYBOARD_MODIFIER_RIGHTALT; keycode = HID_KEY_BRACKET_RIGHT; modifier |= KEYBOARD_MODIFIER_LEFTSHIFT; break;
        case '\\': keycode = HID_KEY_BACKSLASH; break;
        case '|': modifier = KEYBOARD_MODIFIER_LEFTSHIFT; keycode = HID_KEY_BACKSLASH; break;
      }
    }
    if (keycode != 0) { hid_hal_press_key(keycode, modifier); }
  }
}
