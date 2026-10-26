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

/* Checked save I/O for ESP-IDF FatFS and host tests. Not a journaling filesystem. */
#ifndef HIGHBOY_GB_SAVE_IO_H
#define HIGHBOY_GB_SAVE_IO_H
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef GB_SAVE_RENAME
#define GB_SAVE_RENAME rename
#endif
#ifndef GB_SAVE_UNLINK
#define GB_SAVE_UNLINK unlink
#endif
#ifndef GB_SAVE_FWRITE
#define GB_SAVE_FWRITE fwrite
#endif
#ifndef GB_SAVE_FSYNC
#define GB_SAVE_FSYNC fsync
#endif
#ifndef GB_SAVE_FCLOSE
#define GB_SAVE_FCLOSE fclose
#endif

#define GB_SAVE_IO_PATH_BYTES 320

#ifdef __cplusplus
extern "C" {
#endif

static inline bool gb_save_sibling(char *out, const char *path, const char *suffix) {
  int n = snprintf(out, GB_SAVE_IO_PATH_BYTES, "%s%s", path, suffix);
  return n > 0 && n < GB_SAVE_IO_PATH_BYTES;
}

/* -1 is an error, including a directory or wrong-sized file; 0 is absent. */
static inline int gb_save_file_state(const char *path, size_t expected_size) {
  struct stat st;
  if (stat(path, &st) != 0)
    return errno == ENOENT ? 0 : -1;
  if (!S_ISREG(st.st_mode) || st.st_size < 0 || (size_t)st.st_size != expected_size)
    return -1;
  return 1;
}

static inline bool gb_save_read_exact(const char *path, uint8_t *out, size_t bytes) {
  FILE *file = fopen(path, "rb");
  if (!file)
    return false;
  bool ok = fread(out, 1, bytes, file) == bytes && !ferror(file);
  if (ok)
    ok = fgetc(file) == EOF && !ferror(file);
  if (GB_SAVE_FCLOSE(file) != 0)
    ok = false;
  return ok;
}

static inline bool gb_save_verify(const char *path, const uint8_t *expected, size_t bytes) {
  uint8_t chunk[256];
  FILE *file = fopen(path, "rb");
  if (!file)
    return false;
  bool ok = true;
  for (size_t pos = 0; pos < bytes && ok;) {
    size_t n = bytes - pos < sizeof(chunk) ? bytes - pos : sizeof(chunk);
    ok = fread(chunk, 1, n, file) == n && !ferror(file);
    if (ok && expected)
      ok = memcmp(chunk, expected + pos, n) == 0;
    pos += n;
  }
  if (ok)
    ok = fgetc(file) == EOF && !ferror(file);
  if (GB_SAVE_FCLOSE(file) != 0)
    ok = false;
  return ok;
}

/* An absent save is valid only when no unfinished sidecars exist. */
static inline const char *
gb_save_load_checked(const char *path, uint8_t *ram, size_t bytes, bool *present) {
  char temporary[GB_SAVE_IO_PATH_BYTES], backup[GB_SAVE_IO_PATH_BYTES];
  *present = false;
  if (!gb_save_sibling(temporary, path, ".tmp") || !gb_save_sibling(backup, path, ".bak"))
    return "Save path is too long";
  int original = gb_save_file_state(path, bytes);
  int prior = gb_save_file_state(backup, bytes);
  struct stat st;
  bool pending = stat(temporary, &st) == 0;
  if (!pending && errno != ENOENT)
    return "Cannot inspect save temporary file";
  if (original < 0)
    return "Existing save is unreadable or wrong-sized; not overwritten";
  if (prior < 0)
    return "Save backup is unreadable or wrong-sized; preserve files";
  if (prior && !gb_save_verify(backup, NULL, bytes))
    return "Cannot read save backup; preserve save files";

  if (!original && prior) {
    /* Bounded recovery of a failed FAT rename sequence. Never discard .tmp. */
    if (GB_SAVE_RENAME(backup, path) != 0)
      return "Save recovery failed; prior save remains in .bak";
    original = 1;
  }
  if (pending)
    return "Unfinished save .tmp found; preserve files and recover on Mac";
  if (!original)
    return NULL;
  if (!gb_save_read_exact(path, ram, bytes))
    return "Existing save read failed; not overwritten";
  *present = true;
  return NULL;
}

static inline const char *
gb_save_commit_checked(const char *path, const uint8_t *ram, size_t bytes) {
  char temporary[GB_SAVE_IO_PATH_BYTES], backup[GB_SAVE_IO_PATH_BYTES];
  if (!gb_save_sibling(temporary, path, ".tmp") || !gb_save_sibling(backup, path, ".bak"))
    return "Save path is too long";
  int original = gb_save_file_state(path, bytes);
  int prior = gb_save_file_state(backup, bytes);
  if (original < 0 || prior < 0 || (!original && prior))
    return "Save state changed; preserve .sav and .bak files";
  if ((original && !gb_save_verify(path, NULL, bytes)) ||
      (prior && !gb_save_verify(backup, NULL, bytes)))
    return "Save read check failed; previous save kept";

  /* Exclusive creation refuses an unfinished/foreign temporary file. */
  int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return "Cannot create save .tmp; previous save kept";
  FILE *file = fdopen(fd, "wb");
  if (!file) {
    close(fd);
    GB_SAVE_UNLINK(temporary);
    return "Cannot open save .tmp; previous save kept";
  }
  bool ok = GB_SAVE_FWRITE(ram, 1, bytes, file) == bytes && !ferror(file);
  if (ok)
    ok = fflush(file) == 0;
  if (ok)
    ok = GB_SAVE_FSYNC(fileno(file)) == 0;
  if (GB_SAVE_FCLOSE(file) != 0)
    ok = false;
  if (ok)
    ok = gb_save_verify(temporary, ram, bytes);
  if (!ok) {
    /* Only this newly created uncommitted file may be removed here. */
    GB_SAVE_UNLINK(temporary);
    return "Save write or verify failed; previous save kept";
  }

  /* FatFS does not replace an existing destination in rename(). Keep a
   * recoverable prior generation instead; the two renames are NOT atomic. */
  if (original) {
    if (prior && GB_SAVE_UNLINK(backup) != 0)
      return "Cannot rotate save backup; .sav and .tmp kept";
    if (GB_SAVE_RENAME(path, backup) != 0)
      return "Save backup rename failed; preserve .sav/.bak/.tmp";
  }
  if (GB_SAVE_RENAME(temporary, path) != 0) {
    if (original && GB_SAVE_RENAME(backup, path) != 0)
      return "Save commit failed; preserve .sav/.bak/.tmp for recovery";
    return "Save commit failed; prior save restored, .tmp kept";
  }
  return NULL;
}
#ifdef __cplusplus
}
#endif

#endif
