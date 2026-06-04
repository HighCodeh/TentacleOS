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

#include <stddef.h>
#include <stdint.h>

#include "class/hid/hid_device.h"

#include "hid_hal.h"

#ifndef HID_KEY_INTERNATIONAL_1
#define HID_KEY_INTERNATIONAL_1 0x87
#endif

#ifndef HID_KEY_NON_US_BACKSLASH
#define HID_KEY_NON_US_BACKSLASH 0x64
#endif

// Short aliases to keep the layout tables readable.
#define MOD_SHIFT KEYBOARD_MODIFIER_LEFTSHIFT
#define MOD_ALTGR KEYBOARD_MODIFIER_RIGHTALT

// ABNT2 UTF-8 byte pairs for accented characters. The lead byte is always
// 0xC3; the values below are the second (trailing) byte of each sequence.
#define UTF8_LOWER_C_CEDILLA_B2 0xA7 // c = 0xC3 0xA7
#define UTF8_UPPER_C_CEDILLA_B2 0x87 // C = 0xC3 0x87
#define UTF8_LOWER_A_ACUTE_B2   0xA1 // a = 0xC3 0xA1
#define UTF8_LOWER_E_ACUTE_B2   0xA9 // e = 0xC3 0xA9
#define UTF8_LOWER_I_ACUTE_B2   0xAD // i = 0xC3 0xAD
#define UTF8_LOWER_O_ACUTE_B2   0xB3 // o = 0xC3 0xB3
#define UTF8_LOWER_U_ACUTE_B2   0xBA // u = 0xC3 0xBA
#define UTF8_LOWER_A_CIRCUM_B2  0xA2 // a = 0xC3 0xA2
#define UTF8_LOWER_E_CIRCUM_B2  0xAA // e = 0xC3 0xAA
#define UTF8_LOWER_O_CIRCUM_B2  0xB4 // o = 0xC3 0xB4
#define UTF8_LOWER_A_TILDE_B2   0xA3 // a = 0xC3 0xA3
#define UTF8_LOWER_O_TILDE_B2   0xB5 // o = 0xC3 0xB5
#define UTF8_LOWER_A_GRAVE_B2   0xA0 // a = 0xC3 0xA0
#define UTF8_2BYTE_LEAD         0xC3

// A single character's HID translation. A "dead key" (e.g. an accent) is
// expressed as an optional prefix press that is sent before the base key:
// type_entry() presses (dead_keycode, dead_modifier) first when non-zero,
// then (keycode, modifier). An all-zero entry is an unmapped character and
// produces no output.
typedef struct {
  uint8_t keycode;
  uint8_t modifier;
  uint8_t dead_keycode;
  uint8_t dead_modifier;
} hid_key_entry_t;

// A UTF-8 two-byte sequence (b0, b1) mapped to a key entry.
typedef struct {
  uint8_t b0;
  uint8_t b1;
  hid_key_entry_t entry;
} hid_utf8_entry_t;

// A keyboard layout: a 128-entry ASCII table indexed directly by byte value,
// plus an optional supplemental table for UTF-8 two-byte sequences.
typedef struct {
  const hid_key_entry_t *ascii; // 128 entries
  const hid_utf8_entry_t *utf8; // NULL when the layout has no UTF-8 entries
  size_t utf8_count;
} hid_layout_t;

// ---------------------------------------------------------------------------
// US (QWERTY) layout
// ---------------------------------------------------------------------------
static const hid_key_entry_t s_ascii_us[128] = {
    [' '] = {HID_KEY_SPACE, 0, 0, 0},

    ['a'] = {HID_KEY_A, 0, 0, 0}, ['b'] = {HID_KEY_B, 0, 0, 0},
    ['c'] = {HID_KEY_C, 0, 0, 0}, ['d'] = {HID_KEY_D, 0, 0, 0},
    ['e'] = {HID_KEY_E, 0, 0, 0}, ['f'] = {HID_KEY_F, 0, 0, 0},
    ['g'] = {HID_KEY_G, 0, 0, 0}, ['h'] = {HID_KEY_H, 0, 0, 0},
    ['i'] = {HID_KEY_I, 0, 0, 0}, ['j'] = {HID_KEY_J, 0, 0, 0},
    ['k'] = {HID_KEY_K, 0, 0, 0}, ['l'] = {HID_KEY_L, 0, 0, 0},
    ['m'] = {HID_KEY_M, 0, 0, 0}, ['n'] = {HID_KEY_N, 0, 0, 0},
    ['o'] = {HID_KEY_O, 0, 0, 0}, ['p'] = {HID_KEY_P, 0, 0, 0},
    ['q'] = {HID_KEY_Q, 0, 0, 0}, ['r'] = {HID_KEY_R, 0, 0, 0},
    ['s'] = {HID_KEY_S, 0, 0, 0}, ['t'] = {HID_KEY_T, 0, 0, 0},
    ['u'] = {HID_KEY_U, 0, 0, 0}, ['v'] = {HID_KEY_V, 0, 0, 0},
    ['w'] = {HID_KEY_W, 0, 0, 0}, ['x'] = {HID_KEY_X, 0, 0, 0},
    ['y'] = {HID_KEY_Y, 0, 0, 0}, ['z'] = {HID_KEY_Z, 0, 0, 0},

    ['A'] = {HID_KEY_A, MOD_SHIFT, 0, 0}, ['B'] = {HID_KEY_B, MOD_SHIFT, 0, 0},
    ['C'] = {HID_KEY_C, MOD_SHIFT, 0, 0}, ['D'] = {HID_KEY_D, MOD_SHIFT, 0, 0},
    ['E'] = {HID_KEY_E, MOD_SHIFT, 0, 0}, ['F'] = {HID_KEY_F, MOD_SHIFT, 0, 0},
    ['G'] = {HID_KEY_G, MOD_SHIFT, 0, 0}, ['H'] = {HID_KEY_H, MOD_SHIFT, 0, 0},
    ['I'] = {HID_KEY_I, MOD_SHIFT, 0, 0}, ['J'] = {HID_KEY_J, MOD_SHIFT, 0, 0},
    ['K'] = {HID_KEY_K, MOD_SHIFT, 0, 0}, ['L'] = {HID_KEY_L, MOD_SHIFT, 0, 0},
    ['M'] = {HID_KEY_M, MOD_SHIFT, 0, 0}, ['N'] = {HID_KEY_N, MOD_SHIFT, 0, 0},
    ['O'] = {HID_KEY_O, MOD_SHIFT, 0, 0}, ['P'] = {HID_KEY_P, MOD_SHIFT, 0, 0},
    ['Q'] = {HID_KEY_Q, MOD_SHIFT, 0, 0}, ['R'] = {HID_KEY_R, MOD_SHIFT, 0, 0},
    ['S'] = {HID_KEY_S, MOD_SHIFT, 0, 0}, ['T'] = {HID_KEY_T, MOD_SHIFT, 0, 0},
    ['U'] = {HID_KEY_U, MOD_SHIFT, 0, 0}, ['V'] = {HID_KEY_V, MOD_SHIFT, 0, 0},
    ['W'] = {HID_KEY_W, MOD_SHIFT, 0, 0}, ['X'] = {HID_KEY_X, MOD_SHIFT, 0, 0},
    ['Y'] = {HID_KEY_Y, MOD_SHIFT, 0, 0}, ['Z'] = {HID_KEY_Z, MOD_SHIFT, 0, 0},

    ['1'] = {HID_KEY_1, 0, 0, 0}, ['2'] = {HID_KEY_2, 0, 0, 0},
    ['3'] = {HID_KEY_3, 0, 0, 0}, ['4'] = {HID_KEY_4, 0, 0, 0},
    ['5'] = {HID_KEY_5, 0, 0, 0}, ['6'] = {HID_KEY_6, 0, 0, 0},
    ['7'] = {HID_KEY_7, 0, 0, 0}, ['8'] = {HID_KEY_8, 0, 0, 0},
    ['9'] = {HID_KEY_9, 0, 0, 0}, ['0'] = {HID_KEY_0, 0, 0, 0},

    ['!'] = {HID_KEY_1, MOD_SHIFT, 0, 0}, ['@'] = {HID_KEY_2, MOD_SHIFT, 0, 0},
    ['#'] = {HID_KEY_3, MOD_SHIFT, 0, 0}, ['$'] = {HID_KEY_4, MOD_SHIFT, 0, 0},
    ['%'] = {HID_KEY_5, MOD_SHIFT, 0, 0}, ['^'] = {HID_KEY_6, MOD_SHIFT, 0, 0},
    ['&'] = {HID_KEY_7, MOD_SHIFT, 0, 0}, ['*'] = {HID_KEY_8, MOD_SHIFT, 0, 0},
    ['('] = {HID_KEY_9, MOD_SHIFT, 0, 0}, [')'] = {HID_KEY_0, MOD_SHIFT, 0, 0},

    ['-'] = {HID_KEY_MINUS, 0, 0, 0},
    ['_'] = {HID_KEY_MINUS, MOD_SHIFT, 0, 0},
    ['='] = {HID_KEY_EQUAL, 0, 0, 0},
    ['+'] = {HID_KEY_EQUAL, MOD_SHIFT, 0, 0},
    ['.'] = {HID_KEY_PERIOD, 0, 0, 0},
    [','] = {HID_KEY_COMMA, 0, 0, 0},
    ['/'] = {HID_KEY_SLASH, 0, 0, 0},
    ['?'] = {HID_KEY_SLASH, MOD_SHIFT, 0, 0},
    [';'] = {HID_KEY_SEMICOLON, 0, 0, 0},
    [':'] = {HID_KEY_SEMICOLON, MOD_SHIFT, 0, 0},
};

// ---------------------------------------------------------------------------
// Brazilian ABNT2 layout
// ---------------------------------------------------------------------------
static const hid_key_entry_t s_ascii_abnt2[128] = {
    [' ']  = {HID_KEY_SPACE, 0, 0, 0},
    ['\n'] = {HID_KEY_ENTER, 0, 0, 0},
    ['\t'] = {HID_KEY_TAB, 0, 0, 0},

    ['a'] = {HID_KEY_A, 0, 0, 0}, ['b'] = {HID_KEY_B, 0, 0, 0},
    ['c'] = {HID_KEY_C, 0, 0, 0}, ['d'] = {HID_KEY_D, 0, 0, 0},
    ['e'] = {HID_KEY_E, 0, 0, 0}, ['f'] = {HID_KEY_F, 0, 0, 0},
    ['g'] = {HID_KEY_G, 0, 0, 0}, ['h'] = {HID_KEY_H, 0, 0, 0},
    ['i'] = {HID_KEY_I, 0, 0, 0}, ['j'] = {HID_KEY_J, 0, 0, 0},
    ['k'] = {HID_KEY_K, 0, 0, 0}, ['l'] = {HID_KEY_L, 0, 0, 0},
    ['m'] = {HID_KEY_M, 0, 0, 0}, ['n'] = {HID_KEY_N, 0, 0, 0},
    ['o'] = {HID_KEY_O, 0, 0, 0}, ['p'] = {HID_KEY_P, 0, 0, 0},
    ['q'] = {HID_KEY_Q, 0, 0, 0}, ['r'] = {HID_KEY_R, 0, 0, 0},
    ['s'] = {HID_KEY_S, 0, 0, 0}, ['t'] = {HID_KEY_T, 0, 0, 0},
    ['u'] = {HID_KEY_U, 0, 0, 0}, ['v'] = {HID_KEY_V, 0, 0, 0},
    ['w'] = {HID_KEY_W, 0, 0, 0}, ['x'] = {HID_KEY_X, 0, 0, 0},
    ['y'] = {HID_KEY_Y, 0, 0, 0}, ['z'] = {HID_KEY_Z, 0, 0, 0},

    ['A'] = {HID_KEY_A, MOD_SHIFT, 0, 0}, ['B'] = {HID_KEY_B, MOD_SHIFT, 0, 0},
    ['C'] = {HID_KEY_C, MOD_SHIFT, 0, 0}, ['D'] = {HID_KEY_D, MOD_SHIFT, 0, 0},
    ['E'] = {HID_KEY_E, MOD_SHIFT, 0, 0}, ['F'] = {HID_KEY_F, MOD_SHIFT, 0, 0},
    ['G'] = {HID_KEY_G, MOD_SHIFT, 0, 0}, ['H'] = {HID_KEY_H, MOD_SHIFT, 0, 0},
    ['I'] = {HID_KEY_I, MOD_SHIFT, 0, 0}, ['J'] = {HID_KEY_J, MOD_SHIFT, 0, 0},
    ['K'] = {HID_KEY_K, MOD_SHIFT, 0, 0}, ['L'] = {HID_KEY_L, MOD_SHIFT, 0, 0},
    ['M'] = {HID_KEY_M, MOD_SHIFT, 0, 0}, ['N'] = {HID_KEY_N, MOD_SHIFT, 0, 0},
    ['O'] = {HID_KEY_O, MOD_SHIFT, 0, 0}, ['P'] = {HID_KEY_P, MOD_SHIFT, 0, 0},
    ['Q'] = {HID_KEY_Q, MOD_SHIFT, 0, 0}, ['R'] = {HID_KEY_R, MOD_SHIFT, 0, 0},
    ['S'] = {HID_KEY_S, MOD_SHIFT, 0, 0}, ['T'] = {HID_KEY_T, MOD_SHIFT, 0, 0},
    ['U'] = {HID_KEY_U, MOD_SHIFT, 0, 0}, ['V'] = {HID_KEY_V, MOD_SHIFT, 0, 0},
    ['W'] = {HID_KEY_W, MOD_SHIFT, 0, 0}, ['X'] = {HID_KEY_X, MOD_SHIFT, 0, 0},
    ['Y'] = {HID_KEY_Y, MOD_SHIFT, 0, 0}, ['Z'] = {HID_KEY_Z, MOD_SHIFT, 0, 0},

    ['1'] = {HID_KEY_1, 0, 0, 0}, ['2'] = {HID_KEY_2, 0, 0, 0},
    ['3'] = {HID_KEY_3, 0, 0, 0}, ['4'] = {HID_KEY_4, 0, 0, 0},
    ['5'] = {HID_KEY_5, 0, 0, 0}, ['6'] = {HID_KEY_6, 0, 0, 0},
    ['7'] = {HID_KEY_7, 0, 0, 0}, ['8'] = {HID_KEY_8, 0, 0, 0},
    ['9'] = {HID_KEY_9, 0, 0, 0}, ['0'] = {HID_KEY_0, 0, 0, 0},

    ['!'] = {HID_KEY_1, MOD_SHIFT, 0, 0}, ['@'] = {HID_KEY_2, MOD_SHIFT, 0, 0},
    ['#'] = {HID_KEY_3, MOD_SHIFT, 0, 0}, ['$'] = {HID_KEY_4, MOD_SHIFT, 0, 0},
    ['%'] = {HID_KEY_5, MOD_SHIFT, 0, 0}, ['&'] = {HID_KEY_7, MOD_SHIFT, 0, 0},
    ['*'] = {HID_KEY_8, MOD_SHIFT, 0, 0}, ['('] = {HID_KEY_9, MOD_SHIFT, 0, 0},
    [')'] = {HID_KEY_0, MOD_SHIFT, 0, 0},

    ['-'] = {HID_KEY_MINUS, 0, 0, 0},
    ['='] = {HID_KEY_EQUAL, 0, 0, 0},
    ['_'] = {HID_KEY_MINUS, MOD_SHIFT, 0, 0},
    ['+'] = {HID_KEY_EQUAL, MOD_SHIFT, 0, 0},
    ['.'] = {HID_KEY_PERIOD, 0, 0, 0},
    [','] = {HID_KEY_COMMA, 0, 0, 0},
    [';'] = {HID_KEY_SLASH, 0, 0, 0},
    [':'] = {HID_KEY_SLASH, MOD_SHIFT, 0, 0},
    ['/'] = {HID_KEY_INTERNATIONAL_1, 0, 0, 0},
    ['?'] = {HID_KEY_INTERNATIONAL_1, MOD_SHIFT, 0, 0},

    ['['] = {HID_KEY_BRACKET_LEFT, MOD_ALTGR, 0, 0},
    ['{'] = {HID_KEY_BRACKET_LEFT, MOD_ALTGR | MOD_SHIFT, 0, 0},
    [']'] = {HID_KEY_BRACKET_RIGHT, MOD_ALTGR, 0, 0},
    ['}'] = {HID_KEY_BRACKET_RIGHT, MOD_ALTGR | MOD_SHIFT, 0, 0},
    ['\\'] = {HID_KEY_NON_US_BACKSLASH, 0, 0, 0},
    ['|'] = {HID_KEY_NON_US_BACKSLASH, MOD_SHIFT, 0, 0},

    // Dead-key punctuation: acute accent dead key (BRACKET_LEFT) then space
    // yields a literal apostrophe; shift+dead key yields a literal quote.
    ['\''] = {HID_KEY_SPACE, 0, HID_KEY_BRACKET_LEFT, 0},
    ['"']  = {HID_KEY_BRACKET_LEFT, MOD_SHIFT, 0, 0},
};

// ABNT2 accented characters (UTF-8 two-byte sequences, lead byte 0xC3).
static const hid_utf8_entry_t s_utf8_abnt2[] = {
    // Cedilla (direct key, no dead-key prefix)
    {UTF8_2BYTE_LEAD, UTF8_LOWER_C_CEDILLA_B2, {HID_KEY_SEMICOLON, 0, 0, 0}},
    {UTF8_2BYTE_LEAD, UTF8_UPPER_C_CEDILLA_B2, {HID_KEY_SEMICOLON, MOD_SHIFT, 0, 0}},

    // Acute accent (dead key = BRACKET_LEFT)
    {UTF8_2BYTE_LEAD, UTF8_LOWER_A_ACUTE_B2, {HID_KEY_A, 0, HID_KEY_BRACKET_LEFT, 0}},
    {UTF8_2BYTE_LEAD, UTF8_LOWER_E_ACUTE_B2, {HID_KEY_E, 0, HID_KEY_BRACKET_LEFT, 0}},
    {UTF8_2BYTE_LEAD, UTF8_LOWER_I_ACUTE_B2, {HID_KEY_I, 0, HID_KEY_BRACKET_LEFT, 0}},
    {UTF8_2BYTE_LEAD, UTF8_LOWER_O_ACUTE_B2, {HID_KEY_O, 0, HID_KEY_BRACKET_LEFT, 0}},
    {UTF8_2BYTE_LEAD, UTF8_LOWER_U_ACUTE_B2, {HID_KEY_U, 0, HID_KEY_BRACKET_LEFT, 0}},

    // Circumflex accent (dead key = APOSTROPHE + SHIFT)
    {UTF8_2BYTE_LEAD, UTF8_LOWER_A_CIRCUM_B2, {HID_KEY_A, 0, HID_KEY_APOSTROPHE, MOD_SHIFT}},
    {UTF8_2BYTE_LEAD, UTF8_LOWER_E_CIRCUM_B2, {HID_KEY_E, 0, HID_KEY_APOSTROPHE, MOD_SHIFT}},
    {UTF8_2BYTE_LEAD, UTF8_LOWER_O_CIRCUM_B2, {HID_KEY_O, 0, HID_KEY_APOSTROPHE, MOD_SHIFT}},

    // Tilde (dead key = APOSTROPHE)
    {UTF8_2BYTE_LEAD, UTF8_LOWER_A_TILDE_B2, {HID_KEY_A, 0, HID_KEY_APOSTROPHE, 0}},
    {UTF8_2BYTE_LEAD, UTF8_LOWER_O_TILDE_B2, {HID_KEY_O, 0, HID_KEY_APOSTROPHE, 0}},

    // Grave accent (dead key = BRACKET_LEFT + SHIFT)
    {UTF8_2BYTE_LEAD, UTF8_LOWER_A_GRAVE_B2, {HID_KEY_A, 0, HID_KEY_BRACKET_LEFT, MOD_SHIFT}},
};

// Registry indexed by ducky_layout_t. To add a layout, add its enum value in
// ducky_parser.h, declare its tables above, and add a row here.
static const hid_layout_t s_layouts[DUCKY_LAYOUT_COUNT] = {
    [DUCKY_LAYOUT_US]    = {s_ascii_us, NULL, 0},
    [DUCKY_LAYOUT_ABNT2] = {s_ascii_abnt2, s_utf8_abnt2,
                            sizeof(s_utf8_abnt2) / sizeof(s_utf8_abnt2[0])},
};

static void type_entry(const hid_key_entry_t *e) {
  if (e->dead_keycode != 0) {
    hid_hal_press_key(e->dead_keycode, e->dead_modifier);
  }
  if (e->keycode != 0) {
    hid_hal_press_key(e->keycode, e->modifier);
  }
}

void hid_layouts_type_string(ducky_layout_t layout, const char *str) {
  if (layout < 0 || layout >= DUCKY_LAYOUT_COUNT) {
    layout = DUCKY_LAYOUT_US;
  }
  const hid_layout_t *L = &s_layouts[layout];

  for (size_t i = 0; str[i] != '\0'; ++i) {
    uint8_t c = (uint8_t)str[i];

    if (c < 0x80) {
      type_entry(&L->ascii[c]);
      continue;
    }

    // UTF-8 two-byte lead: resolve via the layout's supplemental table.
    uint8_t c2 = (uint8_t)str[i + 1];
    if ((c & 0xE0) == 0xC0 && c2 != '\0' && L->utf8 != NULL) {
      for (size_t k = 0; k < L->utf8_count; ++k) {
        if (L->utf8[k].b0 == c && L->utf8[k].b1 == c2) {
          type_entry(&L->utf8[k].entry);
          i++; // consume the trailing byte
          break;
        }
      }
    }
    // Unmapped / unknown multibyte: produce nothing (matches prior behavior).
  }
}
