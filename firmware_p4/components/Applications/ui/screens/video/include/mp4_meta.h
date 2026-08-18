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

#ifndef MP4_META_H
#define MP4_META_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief Video codec of the first video track, detected from the sample-entry fourcc. */
typedef enum {
  MP4_VCODEC_NONE = 0,  ///< no video track
  MP4_VCODEC_H264,      ///< avc1 / avc3 — NOT decodable in realtime on ESP32-P4 (no HW decoder)
  MP4_VCODEC_MJPEG,     ///< mjpa / jpeg / MJPG — decodable via the HW JPEG engine
  MP4_VCODEC_MPEG4,     ///< mp4v — not supported
  MP4_VCODEC_OTHER,
} mp4_vcodec_t;

/** @brief Cover-art image format (from the iTunes 'covr' data-atom type flag). */
typedef enum {
  MP4_COVER_NONE = 0,
  MP4_COVER_JPEG,  ///< covr data type 13
  MP4_COVER_PNG,   ///< covr data type 14
} mp4_cover_fmt_t;

/** @brief Parsed MP4 metadata. Strings are always NUL-terminated (empty if absent). */
typedef struct {
  char title[64];   ///< ilst  \xA9nam
  char artist[64];  ///< ilst  \xA9ART
  char album[64];   ///< ilst  \xA9alb
  char year[8];     ///< ilst  \xA9day (first 4 chars)

  uint32_t duration_sec;  ///< from mvhd (duration / timescale)
  uint16_t width;         ///< first video track sample-entry width
  uint16_t height;        ///< first video track sample-entry height
  mp4_vcodec_t vcodec;
  bool has_audio;         ///< an 'soun' handler track is present

  mp4_cover_fmt_t cover_fmt;  ///< embedded cover art (from 'covr'); read via mp4_meta_read_cover()
  long cover_off;             ///< absolute file offset of the image bytes (0 if none)
  uint32_t cover_size;        ///< size of the image blob in bytes
} mp4_meta_t;

/**
 * @brief Parse the metadata of an .mp4 / .m4v / .mov file. Reads only the box
 *        tree (moov/udta/ilst + track sample entries), never the media data.
 *        Self-contained, no decoder needed. Returns false if the file is not a
 *        readable ISO-BMFF container.
 */
bool mp4_meta_parse(const char *path, mp4_meta_t *out);

/**
 * @brief Read the embedded cover-art blob into a freshly malloc'd buffer that
 *        the caller must free(). Returns NULL if there is no cover or on error;
 *        on success *out_size holds the byte count.
 */
uint8_t *mp4_meta_read_cover(const char *path, const mp4_meta_t *m, uint32_t *out_size);

/** @brief Human-readable codec name for the status line. */
const char *mp4_vcodec_name(mp4_vcodec_t c);

#ifdef __cplusplus
}
#endif

#endif // MP4_META_H
