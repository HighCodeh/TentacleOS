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

#include "hid_layouts.h"

#include <stdbool.h>

#include "esp_log.h"
#include "class/hid/hid_device.h"

#include "hid_hal.h"

static const char *TAG = "HID_LAYOUTS";

#ifndef HID_KEY_INTERNATIONAL_1
#define HID_KEY_INTERNATIONAL_1 0x87
#endif

#ifndef HID_KEY_NON_US_BACKSLASH
#define HID_KEY_NON_US_BACKSLASH 0x64
#endif

static bool try_decode_abnt2_utf8(uint8_t c1, uint8_t c2);

void hid_layouts_type_string_us(const char *str) {
  for (size_t i = 0; str[i] != '\0'; ++i) {
    char c = str[i];
    uint8_t keycode = 0;
    uint8_t modifier = 0;

    if (c >= 'a' && c <= 'z') {
      keycode = HID_KEY_A + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
      keycode = HID_KEY_A + (c - 'A');
      modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
    } else if (c >= '1' && c <= '9') {
      keycode = HID_KEY_1 + (c - '1');
    } else if (c == '0') {
      keycode = HID_KEY_0;
    } else {
      switch (c) {
        case ' ':
          keycode = HID_KEY_SPACE;
          break;
        case '!':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_1;
          break;
        case '@':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_2;
          break;
        case '#':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_3;
          break;
        case '$':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_4;
          break;
        case '%':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_5;
          break;
        case '^':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_6;
          break;
        case '&':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_7;
          break;
        case '*':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_8;
          break;
        case '(':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_9;
          break;
        case ')':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_0;
          break;
        case '-':
          keycode = HID_KEY_MINUS;
          break;
        case '_':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_MINUS;
          break;
        case '=':
          keycode = HID_KEY_EQUAL;
          break;
        case '+':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_EQUAL;
          break;
        case '.':
          keycode = HID_KEY_PERIOD;
          break;
        case ',':
          keycode = HID_KEY_COMMA;
          break;
        case '/':
          keycode = HID_KEY_SLASH;
          break;
        case '?':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_SLASH;
          break;
        case ':':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_SEMICOLON;
          break;
        case ';':
          keycode = HID_KEY_SEMICOLON;
          break;
        case '\'':
          keycode = HID_KEY_APOSTROPHE;
          break;
        case '`':
          keycode = HID_KEY_GRAVE;
          break;
        case '\\':
          keycode = HID_KEY_BACKSLASH;
          break;
        case '|':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_BACKSLASH;
          break;
        default:
          break;
      }
    }

    if (keycode != 0) {
      hid_hal_press_key(keycode, modifier);
    }
  }
}

void hid_layouts_type_string_abnt2(const char *str) {
  for (size_t i = 0; str[i] != '\0'; ++i) {
    uint8_t c1 = (uint8_t)str[i];
    uint8_t c2 = (uint8_t)str[i + 1];

    // Apostrophe and double quote sit on the key left of '1' (US grave position).
    if (c1 == '\'') {
      hid_hal_press_key(HID_KEY_GRAVE, 0);
      continue;
    }
    if (c1 == '"') {
      hid_hal_press_key(HID_KEY_GRAVE, KEYBOARD_MODIFIER_LEFTSHIFT);
      continue;
    }

    // Backtick is the grave dead key (Shift + acute key); space emits the literal.
    if (c1 == '`') {
      hid_hal_press_key(HID_KEY_BRACKET_LEFT, KEYBOARD_MODIFIER_LEFTSHIFT);
      hid_hal_press_key(HID_KEY_SPACE, 0);
      continue;
    }

    // Circumflex/tilde are dead keys; a trailing space emits the literal char.
    if (c1 == '^') {
      hid_hal_press_key(HID_KEY_APOSTROPHE, KEYBOARD_MODIFIER_LEFTSHIFT);
      hid_hal_press_key(HID_KEY_SPACE, 0);
      continue;
    }
    if (c1 == '~') {
      hid_hal_press_key(HID_KEY_APOSTROPHE, 0);
      hid_hal_press_key(HID_KEY_SPACE, 0);
      continue;
    }

    // Try UTF-8 two-byte sequences (accented characters)
    if ((c1 & 0xE0) == 0xC0 && c2 != '\0') {
      if (try_decode_abnt2_utf8(c1, c2)) {
        i++; // Skip second byte
        continue;
      }
    }

    uint8_t keycode = 0;
    uint8_t modifier = 0;

    if (c1 >= 'a' && c1 <= 'z') {
      keycode = HID_KEY_A + (c1 - 'a');
    } else if (c1 >= 'A' && c1 <= 'Z') {
      modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
      keycode = HID_KEY_A + (c1 - 'A');
    } else if (c1 >= '1' && c1 <= '9') {
      keycode = HID_KEY_1 + (c1 - '1');
    } else if (c1 == '0') {
      keycode = HID_KEY_0;
    } else {
      switch (c1) {
        case '!':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_1;
          break;
        case '@':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_2;
          break;
        case '#':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_3;
          break;
        case '$':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_4;
          break;
        case '%':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_5;
          break;
        case '&':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_7;
          break;
        case '*':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_8;
          break;
        case '(':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_9;
          break;
        case ')':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_0;
          break;
        case ' ':
          keycode = HID_KEY_SPACE;
          break;
        case '\n':
          keycode = HID_KEY_ENTER;
          break;
        case '\t':
          keycode = HID_KEY_TAB;
          break;
        case '-':
          keycode = HID_KEY_MINUS;
          break;
        case '=':
          keycode = HID_KEY_EQUAL;
          break;
        case '_':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_MINUS;
          break;
        case '+':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_EQUAL;
          break;
        case '.':
          keycode = HID_KEY_PERIOD;
          break;
        case ',':
          keycode = HID_KEY_COMMA;
          break;
        case '<':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_COMMA;
          break;
        case '>':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_PERIOD;
          break;
        case ';':
          keycode = HID_KEY_SLASH;
          break;
        case ':':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_SLASH;
          break;
        case '/':
          keycode = HID_KEY_INTERNATIONAL_1;
          break;
        case '?':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_INTERNATIONAL_1;
          break;
        case '[':
          modifier = KEYBOARD_MODIFIER_RIGHTALT;
          keycode = HID_KEY_BRACKET_LEFT;
          break;
        case '{':
          modifier = KEYBOARD_MODIFIER_RIGHTALT | KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_BRACKET_LEFT;
          break;
        case ']':
          modifier = KEYBOARD_MODIFIER_RIGHTALT;
          keycode = HID_KEY_BRACKET_RIGHT;
          break;
        case '}':
          modifier = KEYBOARD_MODIFIER_RIGHTALT | KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_BRACKET_RIGHT;
          break;
        case '\\':
          keycode = HID_KEY_NON_US_BACKSLASH;
          break;
        case '|':
          modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
          keycode = HID_KEY_NON_US_BACKSLASH;
          break;
        default:
          break;
      }
    }

    if (keycode != 0) {
      hid_hal_press_key(keycode, modifier);
    }
  }
}

typedef struct {
  uint8_t c1; // UTF-8 lead byte (0xC2 or 0xC3)
  uint8_t c2; // UTF-8 continuation byte
  uint8_t k1; // dead key or direct key
  uint8_t m1; // modifier for k1
  uint8_t k2; // base letter, 0 for a single keypress
  uint8_t m2; // modifier for k2 (shift = uppercase)
} abnt2_utf8_entry_t;

// ABNT2 dead-key prefix (key, modifier) pressed before the base letter.
#define DK_ACUTE  HID_KEY_BRACKET_LEFT, 0
#define DK_GRAVE  HID_KEY_BRACKET_LEFT, KEYBOARD_MODIFIER_LEFTSHIFT
#define DK_CIRCUM HID_KEY_APOSTROPHE, KEYBOARD_MODIFIER_LEFTSHIFT
#define DK_TILDE  HID_KEY_APOSTROPHE, 0
#define DK_DIAER  HID_KEY_6, KEYBOARD_MODIFIER_LEFTSHIFT

static const abnt2_utf8_entry_t ABNT2_UTF8_MAP[] = {
    {0xC3, 0xA7, HID_KEY_SEMICOLON, 0, 0, 0},                           // ç
    {0xC3, 0x87, HID_KEY_SEMICOLON, KEYBOARD_MODIFIER_LEFTSHIFT, 0, 0}, // Ç
    {0xC3, 0xA0, DK_GRAVE, HID_KEY_A, 0},                               // à
    {0xC3, 0xA1, DK_ACUTE, HID_KEY_A, 0},                               // á
    {0xC3, 0xA2, DK_CIRCUM, HID_KEY_A, 0},                              // â
    {0xC3, 0xA3, DK_TILDE, HID_KEY_A, 0},                               // ã
    {0xC3, 0xA4, DK_DIAER, HID_KEY_A, 0},                               // ä
    {0xC3, 0xA8, DK_GRAVE, HID_KEY_E, 0},                               // è
    {0xC3, 0xA9, DK_ACUTE, HID_KEY_E, 0},                               // é
    {0xC3, 0xAA, DK_CIRCUM, HID_KEY_E, 0},                              // ê
    {0xC3, 0xAB, DK_DIAER, HID_KEY_E, 0},                               // ë
    {0xC3, 0xAC, DK_GRAVE, HID_KEY_I, 0},                               // ì
    {0xC3, 0xAD, DK_ACUTE, HID_KEY_I, 0},                               // í
    {0xC3, 0xAE, DK_CIRCUM, HID_KEY_I, 0},                              // î
    {0xC3, 0xAF, DK_DIAER, HID_KEY_I, 0},                               // ï
    {0xC3, 0xB1, DK_TILDE, HID_KEY_N, 0},                               // ñ
    {0xC3, 0xB2, DK_GRAVE, HID_KEY_O, 0},                               // ò
    {0xC3, 0xB3, DK_ACUTE, HID_KEY_O, 0},                               // ó
    {0xC3, 0xB4, DK_CIRCUM, HID_KEY_O, 0},                              // ô
    {0xC3, 0xB5, DK_TILDE, HID_KEY_O, 0},                               // õ
    {0xC3, 0xB6, DK_DIAER, HID_KEY_O, 0},                               // ö
    {0xC3, 0xB9, DK_GRAVE, HID_KEY_U, 0},                               // ù
    {0xC3, 0xBA, DK_ACUTE, HID_KEY_U, 0},                               // ú
    {0xC3, 0xBB, DK_CIRCUM, HID_KEY_U, 0},                              // û
    {0xC3, 0xBC, DK_DIAER, HID_KEY_U, 0},                               // ü
    {0xC3, 0x80, DK_GRAVE, HID_KEY_A, KEYBOARD_MODIFIER_LEFTSHIFT},     // À
    {0xC3, 0x81, DK_ACUTE, HID_KEY_A, KEYBOARD_MODIFIER_LEFTSHIFT},     // Á
    {0xC3, 0x82, DK_CIRCUM, HID_KEY_A, KEYBOARD_MODIFIER_LEFTSHIFT},    // Â
    {0xC3, 0x83, DK_TILDE, HID_KEY_A, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ã
    {0xC3, 0x84, DK_DIAER, HID_KEY_A, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ä
    {0xC3, 0x88, DK_GRAVE, HID_KEY_E, KEYBOARD_MODIFIER_LEFTSHIFT},     // È
    {0xC3, 0x89, DK_ACUTE, HID_KEY_E, KEYBOARD_MODIFIER_LEFTSHIFT},     // É
    {0xC3, 0x8A, DK_CIRCUM, HID_KEY_E, KEYBOARD_MODIFIER_LEFTSHIFT},    // Ê
    {0xC3, 0x8B, DK_DIAER, HID_KEY_E, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ë
    {0xC3, 0x8C, DK_GRAVE, HID_KEY_I, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ì
    {0xC3, 0x8D, DK_ACUTE, HID_KEY_I, KEYBOARD_MODIFIER_LEFTSHIFT},     // Í
    {0xC3, 0x8E, DK_CIRCUM, HID_KEY_I, KEYBOARD_MODIFIER_LEFTSHIFT},    // Î
    {0xC3, 0x8F, DK_DIAER, HID_KEY_I, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ï
    {0xC3, 0x91, DK_TILDE, HID_KEY_N, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ñ
    {0xC3, 0x92, DK_GRAVE, HID_KEY_O, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ò
    {0xC3, 0x93, DK_ACUTE, HID_KEY_O, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ó
    {0xC3, 0x94, DK_CIRCUM, HID_KEY_O, KEYBOARD_MODIFIER_LEFTSHIFT},    // Ô
    {0xC3, 0x95, DK_TILDE, HID_KEY_O, KEYBOARD_MODIFIER_LEFTSHIFT},     // Õ
    {0xC3, 0x96, DK_DIAER, HID_KEY_O, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ö
    {0xC3, 0x99, DK_GRAVE, HID_KEY_U, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ù
    {0xC3, 0x9A, DK_ACUTE, HID_KEY_U, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ú
    {0xC3, 0x9B, DK_CIRCUM, HID_KEY_U, KEYBOARD_MODIFIER_LEFTSHIFT},    // Û
    {0xC3, 0x9C, DK_DIAER, HID_KEY_U, KEYBOARD_MODIFIER_LEFTSHIFT},     // Ü
    {0xC2, 0xA2, HID_KEY_5, KEYBOARD_MODIFIER_RIGHTALT, 0, 0},          // ¢
    {0xC2, 0xA3, HID_KEY_4, KEYBOARD_MODIFIER_RIGHTALT, 0, 0},          // £
    {0xC2, 0xA7, HID_KEY_EQUAL, KEYBOARD_MODIFIER_RIGHTALT, 0, 0},      // §
    {0xC2, 0xAC, HID_KEY_6, KEYBOARD_MODIFIER_RIGHTALT, 0, 0},          // ¬
    {0xC2, 0xB2, HID_KEY_2, KEYBOARD_MODIFIER_RIGHTALT, 0, 0},          // ²
    {0xC2, 0xB3, HID_KEY_3, KEYBOARD_MODIFIER_RIGHTALT, 0, 0},          // ³
    {0xC2, 0xB9, HID_KEY_1, KEYBOARD_MODIFIER_RIGHTALT, 0, 0},          // ¹
};

#undef DK_ACUTE
#undef DK_GRAVE
#undef DK_CIRCUM
#undef DK_TILDE
#undef DK_DIAER

static bool try_decode_abnt2_utf8(uint8_t c1, uint8_t c2) {
  for (size_t i = 0; i < sizeof(ABNT2_UTF8_MAP) / sizeof(ABNT2_UTF8_MAP[0]); ++i) {
    const abnt2_utf8_entry_t *e = &ABNT2_UTF8_MAP[i];
    if (e->c1 == c1 && e->c2 == c2) {
      hid_hal_press_key(e->k1, e->m1);
      if (e->k2 != 0) {
        hid_hal_press_key(e->k2, e->m2);
      }
      return true;
    }
  }
  return false;
}
