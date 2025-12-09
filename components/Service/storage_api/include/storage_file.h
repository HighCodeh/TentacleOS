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
 * @file storage_file.h
 * @brief File operations interface
 */

#ifndef STORAGE_FILE_H
#define STORAGE_FILE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char path[256];
    size_t size;
    time_t modified_time;
    time_t created_time;
    bool is_directory;
    bool is_hidden;
    bool is_readonly;
} storage_file_info_t;

bool storage_file_exists(const char *path);
esp_err_t storage_file_is_empty(const char *path, bool *empty);

esp_err_t storage_file_get_info(const char *path, storage_file_info_t *info);
esp_err_t storage_file_get_size(const char *path, size_t *size);
esp_err_t storage_file_get_extension(const char *path, char *ext, size_t size);

esp_err_t storage_file_delete(const char *path);
esp_err_t storage_file_rename(const char *old_path, const char *new_path);
esp_err_t storage_file_copy(const char *src, const char *dst);
esp_err_t storage_file_move(const char *src, const char *dst);
esp_err_t storage_file_truncate(const char *path, size_t size);
esp_err_t storage_file_clear(const char *path);
esp_err_t storage_file_compare(const char *path1, const char *path2, bool *equal);

#ifdef __cplusplus
}
#endif

#endif