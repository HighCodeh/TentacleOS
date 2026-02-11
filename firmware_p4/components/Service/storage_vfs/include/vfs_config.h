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
 * @file vfs_config.h
 * @brief VFS backend configuration (edit only one line)
 */

#ifndef VFS_CONFIG_H
#define VFS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define VFS_USE_SD_CARD // Active backend
// #define VFS_USE_SPIFFS
// #define VFS_USE_LITTLEFS
// #define VFS_USE_RAMFS

/* ============================================================================
 * BACKEND CONFIGURATION
 * ============================================================================ */

#ifdef VFS_USE_SD_CARD
    #define VFS_MOUNT_POINT     "/sdcard"
    #define VFS_MAX_FILES       10
    #define VFS_FORMAT_ON_FAIL  false
    #define VFS_BACKEND_NAME    "SD Card"
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
    #define VFS_MOUNT_POINT     "/ram"
    #define VFS_MAX_FILES       10
    #define VFS_RAMFS_SIZE      (512 * 1024)  // 512KB
    #define VFS_BACKEND_NAME    "RAM Disk"
#endif

/* ============================================================================
 * VALIDATION
 * ============================================================================ */

#define VFS_BACKEND_COUNT ( \
    (defined(VFS_USE_SD_CARD) ? 1 : 0) + \
    (defined(VFS_USE_SPIFFS) ? 1 : 0) + \
    (defined(VFS_USE_LITTLEFS) ? 1 : 0) + \
    (defined(VFS_USE_RAMFS) ? 1 : 0) \
)

#if VFS_BACKEND_COUNT == 0
    #error "No VFS backend selected! Uncomment one option in vfs_config.h"
#elif VFS_BACKEND_COUNT > 1
    #error "Multiple VFS backends selected! Only one must be active in vfs_config.h"
#endif

#ifdef __cplusplus
}
#endif

#endif
