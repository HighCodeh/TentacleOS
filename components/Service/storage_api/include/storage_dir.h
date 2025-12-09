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
 * @file storage_dir.h
 * @brief Directory operations interface
 */

#ifndef STORAGE_DIR_H
#define STORAGE_DIR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "storage_file.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*storage_dir_callback_t)(const char *name, bool is_dir, void *user_data);

esp_err_t storage_dir_create(const char *path);
esp_err_t storage_dir_remove(const char *path);
esp_err_t storage_dir_remove_recursive(const char *path);

bool storage_dir_exists(const char *path);
esp_err_t storage_dir_is_empty(const char *path, bool *empty);

esp_err_t storage_dir_list(const char *path, storage_dir_callback_t callback, void *user_data);
esp_err_t storage_dir_count(const char *path, uint32_t *file_count, uint32_t *dir_count);

esp_err_t storage_dir_copy_recursive(const char *src, const char *dst);
esp_err_t storage_dir_get_size(const char *path, uint64_t *total_size);

#ifdef __cplusplus
}
#endif

#endif