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

#ifndef STORAGE_IMPL_H
#define STORAGE_IMPL_H

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

typedef void (*storage_dir_callback_t)(const char *name, bool is_dir, void *user_data);

typedef struct {
    char backend_name[32];
    char mount_point[64];
    bool is_mounted;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t used_bytes;
    uint32_t block_size;
} storage_info_t;

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
esp_err_t storage_dir_create(const char *path);
esp_err_t storage_dir_remove(const char *path);
esp_err_t storage_dir_remove_recursive(const char *path);
bool storage_dir_exists(const char *path);
esp_err_t storage_dir_is_empty(const char *path, bool *empty);
esp_err_t storage_dir_list(const char *path, storage_dir_callback_t callback, void *user_data);
esp_err_t storage_dir_count(const char *path, uint32_t *file_count, uint32_t *dir_count);
esp_err_t storage_dir_copy_recursive(const char *src, const char *dst);
esp_err_t storage_dir_get_size(const char *path, uint64_t *total_size);
esp_err_t storage_get_info(storage_info_t *info);
void storage_print_info_detailed(void);
esp_err_t storage_get_free_space(uint64_t *free_bytes);
esp_err_t storage_get_total_space(uint64_t *total_bytes);
esp_err_t storage_get_used_space(uint64_t *used_bytes);
esp_err_t storage_get_usage_percent(float *percentage);
const char* storage_get_backend_type(void);
const char* storage_get_mount_point_str(void);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_IMPL_H