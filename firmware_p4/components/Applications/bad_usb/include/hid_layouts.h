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

#ifndef HID_LAYOUTS_H
#define HID_LAYOUTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "ducky_parser.h"

/**
 * @brief Type a string using the given keyboard layout.
 *
 * Each layout is a data table that maps characters to HID keycodes and
 * modifiers (see hid_layouts.c). ASCII bytes are looked up by direct index;
 * UTF-8 two-byte sequences (e.g. accented characters) are resolved through a
 * per-layout supplemental table. Dead-key sequences are expressed as an
 * optional prefix press in the table entry, so they need no special-case
 * code here.
 *
 * @param layout  Keyboard layout to use. Out-of-range values fall back to US.
 * @param str     Null-terminated UTF-8 string to type.
 */
void hid_layouts_type_string(ducky_layout_t layout, const char *str);

#ifdef __cplusplus
}
#endif

#endif // HID_LAYOUTS_H
