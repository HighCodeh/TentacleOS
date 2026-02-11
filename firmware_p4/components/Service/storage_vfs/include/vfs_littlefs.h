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

#ifndef VFS_LITTLEFS_H
#define VFS_LITTLEFS_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t vfs_littlefs_init(void);
esp_err_t vfs_littlefs_deinit(void);

bool vfs_littlefs_is_mounted(void);
void vfs_littlefs_print_info(void);

esp_err_t vfs_littlefs_format(void);
esp_err_t vfs_register_littlefs_backend(void);
esp_err_t vfs_unregister_littlefs_backend(void);

#ifdef __cplusplus
}
#endif

#endif // VFS_LITTLEFS_H
