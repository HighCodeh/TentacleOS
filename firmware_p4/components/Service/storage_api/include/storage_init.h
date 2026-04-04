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
 * @file storage_init.h
 * @brief Storage initialization - Backend agnostic
 * 
 * Automatically initializes the backend selected in vfs_config.h
 * Supports: SD Card, SPIFFS, LittleFS, RAMFS
 */

#ifndef STORAGE_INIT_H
#define STORAGE_INIT_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t storage_init(void);
esp_err_t storage_init_custom(uint8_t max_files, bool format_if_failed);
esp_err_t storage_deinit(void);

bool storage_is_mounted(void);
esp_err_t storage_remount(void);
esp_err_t storage_check_health(void);

const char* storage_get_mount_point(void);
const char* storage_get_backend_name(void);
void storage_print_info(void);

#ifdef __cplusplus
}
#endif

#endif