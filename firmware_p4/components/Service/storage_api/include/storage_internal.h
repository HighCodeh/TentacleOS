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

/**
 * @file storage_internal.h
 * @brief Shared utilities for storage_api source files (not public API).
 */

#ifndef STORAGE_INTERNAL_H
#define STORAGE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "vfs_config.h"

/**
 * @brief Resolve a relative or absolute path to a full VFS path.
 *
 * If the path already starts with VFS_MOUNT_POINT, it is copied as-is.
 * If it starts with '/', the mount point is prepended without a separator.
 * Otherwise, mount point + '/' is prepended.
 *
 * @param path       Input path (relative or absolute).
 * @param out_full   Output buffer for the resolved path.
 * @param out_size   Size of the output buffer.
 */
static inline void storage_resolve_path(const char *path, char *out_full, size_t out_size) {
  if (strncmp(path, VFS_MOUNT_POINT, strlen(VFS_MOUNT_POINT)) == 0) {
    snprintf(out_full, out_size, "%s", path);
  } else if (path[0] == '/') {
    snprintf(out_full, out_size, "%s%s", VFS_MOUNT_POINT, path);
  } else {
    snprintf(out_full, out_size, "%s/%s", VFS_MOUNT_POINT, path);
  }
}

#ifdef __cplusplus
}
#endif

#endif // STORAGE_INTERNAL_H
