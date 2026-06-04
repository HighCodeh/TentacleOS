// Host-test shim for hid_hal.h. The equivalence harness (equiv_test.c) provides
// the definition of hid_hal_press_key(), which records each press into a log
// instead of touching USB.
#ifndef SHIM_HID_HAL_H
#define SHIM_HID_HAL_H

#include <stdint.h>

void hid_hal_press_key(uint8_t keycode, uint8_t modifiers);

#endif // SHIM_HID_HAL_H
