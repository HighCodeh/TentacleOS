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

/**
 * @file vfs_config.h
 * @brief VFS backend configuration (edit only one line)
 */

#ifndef VFS_CONFIG_H
#define VFS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// #define VFS_USE_SD_CARD  // micro-SD lives on the P4; the C5 stores on flash
// #define VFS_USE_SPIFFS
#define VFS_USE_LITTLEFS // Active backend
// #define VFS_USE_RAMFS

// Backend Configuration

#ifdef VFS_USE_SD_CARD
#define VFS_MOUNT_POINT    "/sdcard"
#define VFS_MAX_FILES      10
#define VFS_FORMAT_ON_FAIL false
#define VFS_BACKEND_NAME   "SD Card"
#endif

#ifdef VFS_USE_SPIFFS
#define VFS_MOUNT_POINT     "/spiffs"
#define VFS_MAX_FILES       10
#define VFS_FORMAT_ON_FAIL  true
#define VFS_PARTITION_LABEL "storage"
#define VFS_BACKEND_NAME    "SPIFFS"
#endif

#ifdef VFS_USE_LITTLEFS
#define VFS_MOUNT_POINT     "/littlefs"
#define VFS_MAX_FILES       10
#define VFS_FORMAT_ON_FAIL  true
#define VFS_PARTITION_LABEL "storage"
#define VFS_BACKEND_NAME    "LittleFS"
#endif

#ifdef VFS_USE_RAMFS
#define VFS_MOUNT_POINT  "/ram"
#define VFS_MAX_FILES    10
#define VFS_RAMFS_SIZE   (512 * 1024) // 512KB
#define VFS_BACKEND_NAME "RAM Disk"
#endif

// Validation

#if !defined(VFS_USE_SD_CARD) && !defined(VFS_USE_SPIFFS) && !defined(VFS_USE_LITTLEFS) && \
    !defined(VFS_USE_RAMFS)
#error "No VFS backend selected! Uncomment one option in vfs_config.h"
#endif

#ifdef __cplusplus
}
#endif

#endif
