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

#ifndef ID3_META_H
#define ID3_META_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Embedded cover-art image format.
 */
typedef enum {
  ID3_COVER_NONE = 0,
  ID3_COVER_JPEG,
  ID3_COVER_PNG,
} id3_cover_fmt_t;

/**
 * @brief Parsed ID3v2 tag. Strings are always NUL-terminated (empty if absent).
 */
typedef struct {
  char title[64];  /**< TIT2 / TT2 */
  char artist[64]; /**< TPE1 / TP1 */
  char album[64];  /**< TALB / TAL */
  char year[8];    /**< TYER / TDRC / TYE */

  id3_cover_fmt_t
      cover_fmt;  /**< Embedded cover-art format (APIC / PIC). Read with id3_meta_read_cover(). */
  long cover_off; /**< Absolute file offset of the image bytes (0 if none). */
  uint32_t cover_size; /**< Size of the image blob in bytes. */

  long audio_off; /**< File offset where audio begins, right after the ID3v2 tag; a decoder should
                     fseek here before searching for the first MP3 frame sync word. */
} id3_meta_t;

/**
 * @brief Parse the ID3v2 tag at the head of an .mp3 file. Reads only the tag
 *        (bounded), never the audio. Self-contained, no library. On a file with
 *        no ID3v2 tag it still succeeds with empty strings and audio_off = 0.
 */
bool id3_meta_parse(const char *path, id3_meta_t *out);

/**
 * @brief Read the embedded cover-art blob into a freshly malloc'd buffer that
 *        the caller must free(). NULL if there is no cover or on error.
 */
uint8_t *id3_meta_read_cover(const char *path, const id3_meta_t *m, uint32_t *out_size);

#ifdef __cplusplus
}
#endif

#endif // ID3_META_H
