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

#ifndef HID_LAYOUTS_H
#define HID_LAYOUTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * @brief Type a string using the US keyboard layout.
 *
 * Translates each character to the corresponding HID keycode + modifier
 * and sends it through the HAL.
 *
 * @param str  Null-terminated string to type.
 */
void type_string_us(const char *str);

/**
 * @brief Type a string using the Brazilian ABNT2 keyboard layout.
 *
 * Handles dead-key sequences for accented characters (e.g. a, e, c)
 * and remapped punctuation keys specific to the ABNT2 layout.
 *
 * @param str  Null-terminated UTF-8 string to type.
 */
void type_string_abnt2(const char *str);

#ifdef __cplusplus
}
#endif

#endif // HID_LAYOUTS_H
