// Host-test shim for TinyUSB's <class/hid/hid_device.h>.
//
// Provides only the HID usage IDs and modifier bitmasks referenced by
// hid_layouts.c. Values mirror the real USB HID definitions so the equivalence
// test exercises the true keycodes. HID_KEY_INTERNATIONAL_1 and
// HID_KEY_NON_US_BACKSLASH are intentionally NOT defined here, so both the old
// and new translation units fall back to their own identical `#ifndef`
// defaults (0x87 / 0x64) — keeping the comparison consistent.
#ifndef SHIM_HID_DEVICE_H
#define SHIM_HID_DEVICE_H

enum {
  HID_KEY_A = 0x04, HID_KEY_B, HID_KEY_C, HID_KEY_D, HID_KEY_E, HID_KEY_F,
  HID_KEY_G, HID_KEY_H, HID_KEY_I, HID_KEY_J, HID_KEY_K, HID_KEY_L, HID_KEY_M,
  HID_KEY_N, HID_KEY_O, HID_KEY_P, HID_KEY_Q, HID_KEY_R, HID_KEY_S, HID_KEY_T,
  HID_KEY_U, HID_KEY_V, HID_KEY_W, HID_KEY_X, HID_KEY_Y, HID_KEY_Z, // 0x04..0x1D
  HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_4, HID_KEY_5,
  HID_KEY_6, HID_KEY_7, HID_KEY_8, HID_KEY_9, HID_KEY_0,            // 0x1E..0x27
  HID_KEY_ENTER,                                                    // 0x28
  HID_KEY_ESCAPE, HID_KEY_BACKSPACE, HID_KEY_TAB, HID_KEY_SPACE,    // 0x29..0x2C
  HID_KEY_MINUS, HID_KEY_EQUAL,                                     // 0x2D..0x2E
  HID_KEY_BRACKET_LEFT, HID_KEY_BRACKET_RIGHT,                      // 0x2F..0x30
  HID_KEY_BACKSLASH, HID_KEY_EUROPE_1,                              // 0x31..0x32
  HID_KEY_SEMICOLON, HID_KEY_APOSTROPHE, HID_KEY_GRAVE,             // 0x33..0x35
  HID_KEY_COMMA, HID_KEY_PERIOD, HID_KEY_SLASH                      // 0x36..0x38
};

enum {
  KEYBOARD_MODIFIER_LEFTCTRL   = 0x01,
  KEYBOARD_MODIFIER_LEFTSHIFT  = 0x02,
  KEYBOARD_MODIFIER_LEFTALT    = 0x04,
  KEYBOARD_MODIFIER_LEFTGUI    = 0x08,
  KEYBOARD_MODIFIER_RIGHTCTRL  = 0x10,
  KEYBOARD_MODIFIER_RIGHTSHIFT = 0x20,
  KEYBOARD_MODIFIER_RIGHTALT   = 0x40,
  KEYBOARD_MODIFIER_RIGHTGUI   = 0x80
};

#endif // SHIM_HID_DEVICE_H
