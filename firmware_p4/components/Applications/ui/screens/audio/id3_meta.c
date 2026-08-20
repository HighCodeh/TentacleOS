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

#include "id3_meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID3_MAX_TAG   (2u * 1024u * 1024u)
#define ID3_MAX_COVER (4u * 1024u * 1024u)

static uint32_t syncsafe32(const uint8_t *p) {
  return ((uint32_t)(p[0] & 0x7f) << 21) | ((uint32_t)(p[1] & 0x7f) << 14) |
         ((uint32_t)(p[2] & 0x7f) << 7) | (uint32_t)(p[3] & 0x7f);
}
static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint32_t be24(const uint8_t *p) {
  return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static void copy_text(char *dst, size_t dstcap, const uint8_t *data, uint32_t len) {
  if (dstcap == 0 || len < 1)
    return;
  uint8_t enc = data[0];
  const uint8_t *s = data + 1;
  uint32_t n = len - 1;
  size_t o = 0;
  if (enc == 1 || enc == 2) {
    for (uint32_t i = 0; i + 1 <= n && o < dstcap - 1; i += 2) {
      uint8_t lo = s[i];
      uint8_t hi = (i + 1 < n) ? s[i + 1] : 0;
      if (lo == 0xFF || lo == 0xFE)
        continue;
      if (hi == 0 && lo >= 0x20 && lo < 0x7F)
        dst[o++] = (char)lo;
    }
  } else {
    for (uint32_t i = 0; i < n && o < dstcap - 1; i++) {
      if (s[i] == 0)
        break;
      dst[o++] = (char)s[i];
    }
  }
  dst[o] = '\0';
}

static void
handle_pic(const uint8_t *frame_data, uint32_t len, long file_base, bool v22, id3_meta_t *out) {
  if (len < 4)
    return;
  uint8_t enc = frame_data[0];
  uint32_t i = 1;
  id3_cover_fmt_t fmt = ID3_COVER_JPEG;

  if (v22) {
    if (len < 1 + 3 + 1)
      return;
    if (memcmp(frame_data + 1, "PNG", 3) == 0)
      fmt = ID3_COVER_PNG;
    else
      fmt = ID3_COVER_JPEG;
    i = 1 + 3;
  } else {
    uint32_t m = i;
    while (m < len && frame_data[m] != 0)
      m++;
    if (m >= len)
      return;
    if ((m - i) >= 9 && memcmp(frame_data + i, "image/png", 9) == 0)
      fmt = ID3_COVER_PNG;
    else
      fmt = ID3_COVER_JPEG;
    i = m + 1;
  }

  if (i >= len)
    return;
  i += 1;

  if (enc == 1 || enc == 2) {
    while (i + 1 < len && !(frame_data[i] == 0 && frame_data[i + 1] == 0))
      i += 2;
    i += 2;
  } else {
    while (i < len && frame_data[i] != 0)
      i++;
    i += 1;
  }
  if (i >= len)
    return;

  out->cover_fmt = fmt;
  out->cover_off = file_base + (long)i;
  out->cover_size = len - i;
}

bool id3_meta_parse(const char *path, id3_meta_t *out) {
  if (path == NULL || out == NULL)
    return false;
  memset(out, 0, sizeof(*out));
  out->cover_fmt = ID3_COVER_NONE;

  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return false;

  uint8_t h[10];
  if (fread(h, 1, 10, f) != 10) {
    fclose(f);
    return false;
  }
  if (memcmp(h, "ID3", 3) != 0 || h[3] == 0xFF || h[4] == 0xFF) {
    fclose(f);
    out->audio_off = 0;
    return true;
  }

  int major = h[3];
  uint8_t flags = h[5];
  uint32_t tag_size = syncsafe32(h + 6);
  out->audio_off = 10 + (long)tag_size + ((flags & 0x10) ? 10 : 0);

  if (tag_size == 0 || tag_size > ID3_MAX_TAG) {
    fclose(f);
    return true;
  }

  uint8_t *buf = malloc(tag_size);
  if (buf == NULL) {
    fclose(f);
    return true;
  }
  if (fread(buf, 1, tag_size, f) != tag_size) {
    free(buf);
    fclose(f);
    return true;
  }
  fclose(f);

  uint32_t p = 0;
  if (flags & 0x40) {
    if (major == 4) {
      if (tag_size >= 4)
        p += syncsafe32(buf);
    } else if (major == 3) {
      if (tag_size >= 4)
        p += 4 + be32(buf);
    }
    if (p >= tag_size) {
      free(buf);
      return true;
    }
  }

  bool v22 = (major == 2);
  uint32_t id_len = v22 ? 3 : 4;
  uint32_t sz_len = v22 ? 3 : 4;
  uint32_t hdr_len = v22 ? 6 : 10;

  while (p + hdr_len <= tag_size) {
    const uint8_t *id = buf + p;
    if (id[0] == 0)
      break;

    uint32_t fsize;
    if (v22)
      fsize = be24(buf + p + id_len);
    else if (major == 4)
      fsize = syncsafe32(buf + p + id_len);
    else
      fsize = be32(buf + p + id_len);

    uint32_t data_off = p + hdr_len;
    if (fsize == 0 || data_off + fsize > tag_size)
      break;
    const uint8_t *data = buf + data_off;
    long file_base = 10 + (long)data_off;

    if (v22) {
      if (memcmp(id, "TT2", 3) == 0)
        copy_text(out->title, sizeof(out->title), data, fsize);
      else if (memcmp(id, "TP1", 3) == 0)
        copy_text(out->artist, sizeof(out->artist), data, fsize);
      else if (memcmp(id, "TAL", 3) == 0)
        copy_text(out->album, sizeof(out->album), data, fsize);
      else if (memcmp(id, "TYE", 3) == 0)
        copy_text(out->year, sizeof(out->year), data, fsize);
      else if (memcmp(id, "PIC", 3) == 0)
        handle_pic(data, fsize, file_base, true, out);
    } else {
      if (memcmp(id, "TIT2", 4) == 0)
        copy_text(out->title, sizeof(out->title), data, fsize);
      else if (memcmp(id, "TPE1", 4) == 0)
        copy_text(out->artist, sizeof(out->artist), data, fsize);
      else if (memcmp(id, "TALB", 4) == 0)
        copy_text(out->album, sizeof(out->album), data, fsize);
      else if (memcmp(id, "TYER", 4) == 0 || memcmp(id, "TDRC", 4) == 0)
        copy_text(out->year, sizeof(out->year), data, fsize);
      else if (memcmp(id, "APIC", 4) == 0)
        handle_pic(data, fsize, file_base, false, out);
    }

    p = data_off + fsize;
    (void)sz_len;
  }

  free(buf);
  return true;
}

uint8_t *id3_meta_read_cover(const char *path, const id3_meta_t *m, uint32_t *out_size) {
  if (path == NULL || m == NULL || m->cover_fmt == ID3_COVER_NONE || m->cover_size == 0)
    return NULL;
  if (m->cover_size > ID3_MAX_COVER)
    return NULL;

  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return NULL;
  uint8_t *buf = malloc(m->cover_size);
  if (buf == NULL) {
    fclose(f);
    return NULL;
  }
  if (fseek(f, m->cover_off, SEEK_SET) != 0 || fread(buf, 1, m->cover_size, f) != m->cover_size) {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  if (out_size)
    *out_size = m->cover_size;
  return buf;
}
