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

// P4-local file operations for the companion host link. The P4 owns both
// filesystems, reached over the standard VFS (POSIX). Paths are sandboxed to
// the mounted roots; ".." is rejected so the app cannot escape them.
//
// Wire layouts (host-link CMD payload → here; response written after the RESP
// status byte by the caller). All multi-byte fields little-endian.
//   FILE_LIST   req: <path>
//               resp: [u16 count][entry...]  entry=[u8 is_dir][u32 size][u8 nlen][name]
//   FILE_STAT   req: <path>
//               resp: [u8 exists][u8 is_dir][u32 size]
//   FILE_READ   req: [u32 offset][u16 len]<path>
//               resp: <bytes>            (0 bytes ⇒ EOF)
//   FILE_WRITE  req: [u32 offset][u8 flags][u16 path_len]<path><data>
//               flags bit0 = CREATE/TRUNCATE; resp: [u32 written]
//   FILE_DELETE req: <path>              resp: (empty)
//   FILE_MKDIR  req: <path>              resp: (empty)

#include "host_link_files.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"

#include "spi_protocol.h"

static const char *TAG = "HOST_LINK_FILES";

#define FILE_PATH_MAX       256
#define FILE_WRITE_TRUNCATE 0x01

// Filesystem roots the companion may touch. Anything else is rejected.
static const char *const ALLOWED_ROOTS[] = {"/assets", "/littlefs", "/sdcard"};

static uint32_t rd_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void wr_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

// Copy a non-NUL-terminated path field into a buffer, validate the sandbox.
static bool path_ok(const uint8_t *src, uint16_t src_len, char *out, size_t out_cap) {
  if (src_len == 0 || src_len >= out_cap) {
    return false;
  }
  memcpy(out, src, src_len);
  out[src_len] = '\0';

  if (strstr(out, "..") != NULL) {
    return false; // no traversal out of the sandbox
  }
  for (size_t i = 0; i < sizeof(ALLOWED_ROOTS) / sizeof(ALLOWED_ROOTS[0]); i++) {
    size_t rlen = strlen(ALLOWED_ROOTS[i]);
    if (strncmp(out, ALLOWED_ROOTS[i], rlen) == 0 && (out[rlen] == '\0' || out[rlen] == '/')) {
      return true;
    }
  }
  return false;
}

bool host_files_is_file_op(uint16_t cmd) {
  return cmd >= SPI_ID_FILE_LIST && cmd <= SPI_ID_FILE_MKDIR;
}

static uint8_t do_list(const char *path, uint8_t *out, uint16_t cap, uint16_t *out_len) {
  DIR *dir = opendir(path);
  if (dir == NULL) {
    return SPI_STATUS_ERROR;
  }

  uint16_t off = 2; // reserve [u16 count]
  uint16_t count = 0;
  struct dirent *e;
  while ((e = readdir(dir)) != NULL) {
    uint8_t nlen = (uint8_t)strnlen(e->d_name, 255);
    char full[FILE_PATH_MAX + 260]; // path + '/' + entry name, no truncation
    struct stat st = {0};
    snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
    stat(full, &st);

    uint16_t entry_len = (uint16_t)(1 + 4 + 1 + nlen);
    if ((uint32_t)off + entry_len > cap) {
      break; // remaining entries don't fit this chunk
    }
    out[off++] = (e->d_type == DT_DIR) ? 1 : 0;
    wr_u32(out + off, (uint32_t)st.st_size);
    off += 4;
    out[off++] = nlen;
    memcpy(out + off, e->d_name, nlen);
    off += nlen;
    count++;
  }
  closedir(dir);

  out[0] = (uint8_t)(count & 0xFF);
  out[1] = (uint8_t)((count >> 8) & 0xFF);
  *out_len = off;
  return SPI_STATUS_OK;
}

static uint8_t do_stat(const char *path, uint8_t *out, uint16_t *out_len) {
  struct stat st = {0};
  bool exists = (stat(path, &st) == 0);
  out[0] = exists ? 1 : 0;
  out[1] = (exists && S_ISDIR(st.st_mode)) ? 1 : 0;
  wr_u32(out + 2, exists ? (uint32_t)st.st_size : 0);
  *out_len = 6;
  return SPI_STATUS_OK;
}

static uint8_t
do_read(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t cap, uint16_t *out_len) {
  if (plen < 6) {
    return SPI_STATUS_INVALID_ARG;
  }
  uint32_t offset = rd_u32(payload);
  uint16_t want = rd_u16(payload + 4);
  char path[FILE_PATH_MAX];
  if (!path_ok(payload + 6, (uint16_t)(plen - 6), path, sizeof(path))) {
    return SPI_STATUS_INVALID_ARG;
  }
  if (want > cap) {
    want = cap;
  }

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    return SPI_STATUS_ERROR;
  }
  if (fseek(f, (long)offset, SEEK_SET) != 0) {
    fclose(f);
    return SPI_STATUS_ERROR;
  }
  size_t got = fread(out, 1, want, f);
  fclose(f);
  *out_len = (uint16_t)got;
  return SPI_STATUS_OK;
}

static uint8_t do_write(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t *out_len) {
  if (plen < 7) {
    return SPI_STATUS_INVALID_ARG;
  }
  uint32_t offset = rd_u32(payload);
  uint8_t flags = payload[4];
  uint16_t path_len = rd_u16(payload + 5);
  if ((uint32_t)7 + path_len > plen) {
    return SPI_STATUS_INVALID_ARG;
  }
  char path[FILE_PATH_MAX];
  if (!path_ok(payload + 7, path_len, path, sizeof(path))) {
    return SPI_STATUS_INVALID_ARG;
  }
  const uint8_t *data = payload + 7 + path_len;
  uint16_t data_len = (uint16_t)(plen - 7 - path_len);

  // Truncate/create starts a fresh file; otherwise update in place at offset
  // (creating the file if absent).
  FILE *f = NULL;
  if (flags & FILE_WRITE_TRUNCATE) {
    f = fopen(path, "wb");
  } else {
    f = fopen(path, "r+b");
    if (f == NULL) {
      f = fopen(path, "wb");
    }
  }
  if (f == NULL) {
    return SPI_STATUS_ERROR;
  }
  if (!(flags & FILE_WRITE_TRUNCATE) && fseek(f, (long)offset, SEEK_SET) != 0) {
    fclose(f);
    return SPI_STATUS_ERROR;
  }
  size_t written = (data_len > 0) ? fwrite(data, 1, data_len, f) : 0;
  fclose(f);

  wr_u32(out, (uint32_t)written);
  *out_len = 4;
  return (written == data_len) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
}

uint8_t host_files_handle(uint16_t cmd,
                          const uint8_t *payload,
                          uint16_t plen,
                          uint8_t *out_data,
                          uint16_t out_cap,
                          uint16_t *out_len) {
  *out_len = 0;
  if (payload == NULL && plen > 0) {
    return SPI_STATUS_INVALID_ARG;
  }

  char path[FILE_PATH_MAX];

  switch (cmd) {
    case SPI_ID_FILE_LIST:
      if (!path_ok(payload, plen, path, sizeof(path)))
        return SPI_STATUS_INVALID_ARG;
      return do_list(path, out_data, out_cap, out_len);

    case SPI_ID_FILE_STAT:
      if (!path_ok(payload, plen, path, sizeof(path)))
        return SPI_STATUS_INVALID_ARG;
      return do_stat(path, out_data, out_len);

    case SPI_ID_FILE_READ:
      return do_read(payload, plen, out_data, out_cap, out_len);

    case SPI_ID_FILE_WRITE:
      return do_write(payload, plen, out_data, out_len);

    case SPI_ID_FILE_DELETE: {
      if (!path_ok(payload, plen, path, sizeof(path)))
        return SPI_STATUS_INVALID_ARG;
      struct stat st = {0};
      if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return (rmdir(path) == 0) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
      return (remove(path) == 0) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_FILE_MKDIR:
      if (!path_ok(payload, plen, path, sizeof(path)))
        return SPI_STATUS_INVALID_ARG;
      return (mkdir(path, 0775) == 0) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    default:
      ESP_LOGW(TAG, "Unhandled file op 0x%04X", cmd);
      return SPI_STATUS_UNSUPPORTED;
  }
}
