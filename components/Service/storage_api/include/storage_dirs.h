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

#ifndef STORAGE_DIRS_H
#define STORAGE_DIRS_H

#include "vfs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_DIR_CONFIG    VFS_MOUNT_POINT "/config"
#define STORAGE_DIR_DATA      VFS_MOUNT_POINT "/data"
#define STORAGE_DIR_LOGS      VFS_MOUNT_POINT "/logs"
#define STORAGE_DIR_CACHE     VFS_MOUNT_POINT "/cache"
#define STORAGE_DIR_TEMP      VFS_MOUNT_POINT "/temp"
#define STORAGE_DIR_BACKUP    VFS_MOUNT_POINT "/backup" 
#define STORAGE_DIR_CERTS     VFS_MOUNT_POINT "/certs"
#define STORAGE_DIR_SCRIPTS   VFS_MOUNT_POINT "/scripts"
#define STORAGE_DIR_CAPTIVE   VFS_MOUNT_POINT "/captive_portal"

#ifdef __cplusplus
}
#endif

#endif // STORAGE_DIRS_H
