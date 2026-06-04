// Host-test shim for hid_layouts.h.
//
// Declares both the NEW generic entry point (compiled from the real
// firmware_p4/.../bad_usb/hid_layouts.c) and the LEGACY per-layout functions
// (compiled from the frozen hid_layouts_reference_old.c), so both translation
// units compile cleanly against one header.
#ifndef SHIM_HID_LAYOUTS_H
#define SHIM_HID_LAYOUTS_H

#include <stddef.h>

#include "ducky_parser.h"

// New, table-driven API (the code under test).
void hid_layouts_type_string(ducky_layout_t layout, const char *str);

// Legacy API (frozen pre-refactor reference, the oracle).
void hid_layouts_type_string_us(const char *str);
void hid_layouts_type_string_abnt2(const char *str);

#endif // SHIM_HID_LAYOUTS_H
