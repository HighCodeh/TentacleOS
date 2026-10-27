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

// Filesystem subsystem of the app ABI (api->fs): read-only access to the SD card,
// gated by TOS_CAP_FS_READ. Every path is sandboxed under /sdcard (no traversal, no
// other mounts), so an app can browse and read user files but never the firmware's
// own assets/config partitions. There is deliberately no write path here.

#include "tos_api.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "tos_app_ctx.h"

// Path must be absolute under /sdcard, with no ".." traversal.
static bool ok_path(const char *p) {
  if (p == NULL || strncmp(p, "/sdcard", 7) != 0)
    return false;
  if (p[7] != '\0' && p[7] != '/') // reject e.g. "/sdcardevil"
    return false;
  if (strstr(p, "..") != NULL)
    return false;
  return true;
}

static int fs_list(const char *path, tos_fs_dirent_t *out, int max) {
  if (!tos_app_cap_check(TOS_CAP_FS_READ) || !ok_path(path) || out == NULL || max <= 0)
    return -1;
  DIR *d = opendir(path);
  if (d == NULL)
    return -1;
  int n = 0;
  struct dirent *e;
  while ((e = readdir(d)) != NULL && n < max) {
    if (e->d_name[0] == '.') // skip ".", "..", hidden
      continue;
    tos_fs_dirent_t *o = &out[n];
    memset(o, 0, sizeof(*o));
    strlcpy(o->name, e->d_name, sizeof(o->name));
    char full[300];
    snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
    struct stat st;
    if (stat(full, &st) == 0) {
      o->is_dir = S_ISDIR(st.st_mode);
      o->size = (uint32_t)st.st_size;
    } else {
      o->is_dir = (e->d_type == DT_DIR);
    }
    n++;
  }
  closedir(d);
  return n;
}

static long fs_size(const char *path) {
  if (!tos_app_cap_check(TOS_CAP_FS_READ) || !ok_path(path))
    return -1;
  struct stat st;
  if (stat(path, &st) != 0)
    return -1;
  return (long)st.st_size;
}

static void *fs_open(const char *path) {
  if (!tos_app_cap_check(TOS_CAP_FS_READ) || !ok_path(path))
    return NULL;
  return (void *)fopen(path, "rb");
}

static int fs_read(void *handle, void *buf, int len) {
  if (!tos_app_cap_check(TOS_CAP_FS_READ) || handle == NULL || buf == NULL || len <= 0)
    return -1;
  return (int)fread(buf, 1, (size_t)len, (FILE *)handle);
}

static int fs_seek(void *handle, long offset) {
  if (!tos_app_cap_check(TOS_CAP_FS_READ) || handle == NULL || offset < 0)
    return -1;
  return fseek((FILE *)handle, offset, SEEK_SET) == 0 ? 0 : -1;
}

static void fs_close(void *handle) {
  if (handle != NULL)
    fclose((FILE *)handle);
}

const tos_fs_api_t tos_fs_api_impl = {
    .list = fs_list,
    .size = fs_size,
    .open = fs_open,
    .read = fs_read,
    .seek = fs_seek,
    .close = fs_close,
};
