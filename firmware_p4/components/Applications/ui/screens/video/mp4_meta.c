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

#include "mp4_meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MP4_META";

#define MP4_MAX_DEPTH       8
#define MP4_MAX_BOXES       4096
#define ILST_STR_MAX        128
#define MP4_MAX_COVER_BYTES (4u * 1024u * 1024u)

typedef struct {
  mp4_meta_t *m;
  uint32_t timescale;
  uint64_t duration;
  int cur_hdlr;
  int boxes_seen;
} mp4_ctx_t;

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint16_t be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
static uint64_t be64(const uint8_t *p) {
  return ((uint64_t)be32(p) << 32) | (uint64_t)be32(p + 4);
}

static bool read_at(FILE *f, long off, void *buf, size_t len) {
  if (fseek(f, off, SEEK_SET) != 0)
    return false;
  return fread(buf, 1, len, f) == len;
}

static void str_copy_trunc(char *dst, size_t dstcap, const char *src, size_t srclen) {
  if (dstcap == 0)
    return;
  size_t k = srclen < dstcap - 1 ? srclen : dstcap - 1;
  memcpy(dst, src, k);
  dst[k] = '\0';
}

static void
handle_ilst_data(FILE *f, const uint8_t *key, long payload_off, long payload_end, mp4_ctx_t *c) {
  uint8_t hd[8];
  if (payload_end - payload_off < 8 || !read_at(f, payload_off, hd, 8))
    return;
  uint32_t type_class = be32(hd) & 0x00FFFFFF;
  long val_off = payload_off + 8;
  long val_len = payload_end - val_off;
  if (val_len <= 0)
    return;

  const uint8_t nam[4] = {0xA9, 'n', 'a', 'm'};
  const uint8_t art[4] = {0xA9, 'A', 'R', 'T'};
  const uint8_t alb[4] = {0xA9, 'a', 'l', 'b'};
  const uint8_t day[4] = {0xA9, 'd', 'a', 'y'};

  char *dst = NULL;
  size_t dstcap = 0;
  if (memcmp(key, nam, 4) == 0) {
    dst = c->m->title;
    dstcap = sizeof(c->m->title);
  } else if (memcmp(key, art, 4) == 0) {
    dst = c->m->artist;
    dstcap = sizeof(c->m->artist);
  } else if (memcmp(key, alb, 4) == 0) {
    dst = c->m->album;
    dstcap = sizeof(c->m->album);
  } else if (memcmp(key, day, 4) == 0) {
    dst = c->m->year;
    dstcap = sizeof(c->m->year);
  } else if (memcmp(key, "covr", 4) == 0) {
    uint8_t t = (uint8_t)(type_class & 0xFF);
    if (t == 13)
      c->m->cover_fmt = MP4_COVER_JPEG;
    else if (t == 14)
      c->m->cover_fmt = MP4_COVER_PNG;
    else
      c->m->cover_fmt = MP4_COVER_JPEG;
    c->m->cover_off = val_off;
    c->m->cover_size = (uint32_t)val_len;
    return;
  }

  if (dst != NULL && dstcap > 0) {
    char tmp[ILST_STR_MAX];
    size_t rd = (size_t)val_len < sizeof(tmp) ? (size_t)val_len : sizeof(tmp);
    if (read_at(f, val_off, tmp, rd))
      str_copy_trunc(dst, dstcap, tmp, rd);
  }
}

static void handle_stsd(FILE *f, long payload_off, long payload_end, mp4_ctx_t *c) {
  uint8_t h[8];
  if (payload_end - payload_off < 8 || !read_at(f, payload_off, h, 8))
    return;
  long entry_off = payload_off + 8;
  uint8_t se[36];
  long avail = payload_end - entry_off;
  size_t rd = avail < (long)sizeof(se) ? (size_t)avail : sizeof(se);
  if (rd < 8 || !read_at(f, entry_off, se, rd))
    return;
  const uint8_t *fourcc = se + 4;

  if (c->cur_hdlr == 'v' && c->m->vcodec == MP4_VCODEC_NONE) {
    if (memcmp(fourcc, "avc1", 4) == 0 || memcmp(fourcc, "avc3", 4) == 0)
      c->m->vcodec = MP4_VCODEC_H264;
    else if (memcmp(fourcc, "mjpa", 4) == 0 || memcmp(fourcc, "jpeg", 4) == 0 ||
             memcmp(fourcc, "MJPG", 4) == 0 || memcmp(fourcc, "mjpg", 4) == 0)
      c->m->vcodec = MP4_VCODEC_MJPEG;
    else if (memcmp(fourcc, "mp4v", 4) == 0)
      c->m->vcodec = MP4_VCODEC_MPEG4;
    else
      c->m->vcodec = MP4_VCODEC_OTHER;
    if (rd >= 28) {
      c->m->width = be16(se + 24);
      c->m->height = be16(se + 26);
    }
  }
}

static void walk(FILE *f, long start, long end, int depth, const uint8_t *ilst_key, mp4_ctx_t *c) {
  if (depth > MP4_MAX_DEPTH)
    return;
  long pos = start;
  while (pos + 8 <= end && c->boxes_seen < MP4_MAX_BOXES) {
    uint8_t hdr[8];
    if (!read_at(f, pos, hdr, 8))
      return;
    c->boxes_seen++;
    uint64_t size = be32(hdr);
    const uint8_t *type = hdr + 4;
    long header = 8;
    if (size == 1) {
      uint8_t big[8];
      if (!read_at(f, pos + 8, big, 8))
        return;
      size = be64(big);
      header = 16;
    } else if (size == 0) {
      size = (uint64_t)(end - pos);
    }
    if (size < (uint64_t)header)
      return;
    long box_end = pos + (long)size;
    if (box_end > end || box_end <= pos)
      box_end = end;
    long payload = pos + header;

    if (ilst_key != NULL) {
      if (memcmp(type, "data", 4) == 0)
        handle_ilst_data(f, ilst_key, payload, box_end, c);
    } else if (memcmp(type, "moov", 4) == 0 || memcmp(type, "trak", 4) == 0 ||
               memcmp(type, "mdia", 4) == 0 || memcmp(type, "minf", 4) == 0 ||
               memcmp(type, "stbl", 4) == 0 || memcmp(type, "udta", 4) == 0) {
      if (memcmp(type, "trak", 4) == 0)
        c->cur_hdlr = 0;
      walk(f, payload, box_end, depth + 1, NULL, c);
    } else if (memcmp(type, "meta", 4) == 0) {
      walk(f, payload + 4, box_end, depth + 1, NULL, c);
    } else if (memcmp(type, "ilst", 4) == 0) {
      long ip = payload;
      while (ip + 8 <= box_end && c->boxes_seen < MP4_MAX_BOXES) {
        uint8_t ih[8];
        if (!read_at(f, ip, ih, 8))
          break;
        c->boxes_seen++;
        uint64_t isz = be32(ih);
        if (isz < 8)
          break;
        long iend = ip + (long)isz;
        if (iend > box_end || iend <= ip)
          break;
        walk(f, ip + 8, iend, depth + 1, ih + 4, c);
        ip = iend;
      }
    } else if (memcmp(type, "mvhd", 4) == 0) {
      uint8_t mv[24];
      if (read_at(f, payload, mv, sizeof(mv))) {
        if (mv[0] == 0) {
          c->timescale = be32(mv + 12);
          c->duration = be32(mv + 16);
        } else {
          c->timescale = be32(mv + 20);
          uint8_t d8[8];
          if (read_at(f, payload + 24, d8, 8))
            c->duration = be64(d8);
        }
      }
    } else if (memcmp(type, "hdlr", 4) == 0) {
      uint8_t hb[12];
      if (read_at(f, payload, hb, sizeof(hb))) {
        if (memcmp(hb + 8, "vide", 4) == 0)
          c->cur_hdlr = 'v';
        else if (memcmp(hb + 8, "soun", 4) == 0) {
          c->cur_hdlr = 's';
          c->m->has_audio = true;
        }
      }
    } else if (memcmp(type, "stsd", 4) == 0) {
      handle_stsd(f, payload, box_end, c);
    }

    pos = box_end;
  }
}

bool mp4_meta_parse(const char *path, mp4_meta_t *out) {
  if (path == NULL || out == NULL)
    return false;
  memset(out, 0, sizeof(*out));
  out->vcodec = MP4_VCODEC_NONE;
  out->cover_fmt = MP4_COVER_NONE;

  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return false;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  long fsize = ftell(f);
  if (fsize < 16) {
    fclose(f);
    return false;
  }

  uint8_t top[8];
  if (!read_at(f, 0, top, 8) ||
      (memcmp(top + 4, "ftyp", 4) != 0 && memcmp(top + 4, "moov", 4) != 0 &&
       memcmp(top + 4, "free", 4) != 0 && memcmp(top + 4, "wide", 4) != 0 &&
       memcmp(top + 4, "mdat", 4) != 0 && memcmp(top + 4, "skip", 4) != 0)) {
    fclose(f);
    return false;
  }

  mp4_ctx_t c = {.m = out};
  walk(f, 0, fsize, 0, NULL, &c);
  fclose(f);

  if (c.timescale > 0)
    out->duration_sec = (uint32_t)(c.duration / c.timescale);

  return true;
}

uint8_t *mp4_meta_read_cover(const char *path, const mp4_meta_t *m, uint32_t *out_size) {
  if (path == NULL || m == NULL || m->cover_fmt == MP4_COVER_NONE || m->cover_size == 0)
    return NULL;
  if (m->cover_size > MP4_MAX_COVER_BYTES)
    return NULL;

  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return NULL;
  uint8_t *buf = malloc(m->cover_size);
  if (buf == NULL) {
    fclose(f);
    return NULL;
  }
  if (!read_at(f, m->cover_off, buf, m->cover_size)) {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  if (out_size)
    *out_size = m->cover_size;
  return buf;
}

const char *mp4_vcodec_name(mp4_vcodec_t c) {
  switch (c) {
    case MP4_VCODEC_H264:
      return "H.264";
    case MP4_VCODEC_MJPEG:
      return "MJPEG";
    case MP4_VCODEC_MPEG4:
      return "MPEG-4";
    case MP4_VCODEC_OTHER:
      return "video";
    default:
      return "no video";
  }
}
