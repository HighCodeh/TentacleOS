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

#ifndef FONT_H
#define FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Bitmap font descriptor for fixed-width rendering.
 */
typedef struct {
  const uint8_t *bitmap; /**< Contiguous glyph bitmap data.       */
  uint16_t first_char;   /**< First character code (ASCII).        */
  uint16_t last_char;    /**< Last character code (ASCII).         */
  uint8_t width;         /**< Glyph width in pixels.               */
  uint8_t height;        /**< Glyph height in pixels.              */
  uint8_t spacing;       /**< Horizontal spacing between glyphs.   */
} font_t;

/** @brief Ubuntu Mono 16px bitmap font (ASCII 32-126). */
extern const font_t font_ubuntu_16;

/** @brief Terminus 24px bitmap font. */
extern const font_t font_terminus_24;

/** @brief Arial 32px bitmap font. */
extern const font_t font_arial_32;

#ifdef __cplusplus
}
#endif

#endif /* FONT_H */
