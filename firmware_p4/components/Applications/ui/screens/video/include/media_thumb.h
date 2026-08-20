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

#ifndef MEDIA_THUMB_H
#define MEDIA_THUMB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

/**
 * @brief A decoded RGB565 image ready to hand to an lv_image. Its `dsc.data`
 *        buffer is owned by this struct; release it with media_thumb_free().
 */
typedef struct {
  lv_image_dsc_t dsc;
  void *buf; ///< backing pixel buffer (freed by media_thumb_free)
  uint16_t w;
  uint16_t h;
} media_thumb_t;

/**
 * @brief Decode a baseline-JPEG blob (e.g. the MP4 'covr' artwork) to an
 *        RGB565 lv_image using the ESP32-P4 hardware JPEG engine. Progressive
 *        JPEG and PNG are NOT supported (returns false) — the caller should
 *        fall back to a placeholder glyph. Run this OFF the LVGL thread.
 *
 * @return true on success (out is filled, free with media_thumb_free), false
 *         on any error (unsupported format, decode failure, OOM).
 */
bool media_thumb_decode_jpeg(const uint8_t *jpeg, size_t len, media_thumb_t *out);

/** @brief Release a media_thumb_t produced by media_thumb_decode_jpeg. */
void media_thumb_free(media_thumb_t *t);

#ifdef __cplusplus
}
#endif

#endif // MEDIA_THUMB_H
