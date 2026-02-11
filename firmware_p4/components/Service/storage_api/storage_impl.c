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
 * @file storage_impl.c
 * @brief File, directory and info operations implementation
 */

#include "storage_impl.h"
#include "storage_init.h"
#include "vfs_config.h"
#include "vfs_core.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "storage";

/* ============================================================================
 * HELPER: Resolve Path
 * ============================================================================ */

static void resolve_path(const char *path, char *full_path, size_t size)
{
    if (strncmp(path, VFS_MOUNT_POINT, strlen(VFS_MOUNT_POINT)) == 0) {
        snprintf(full_path, size, "%s", path);
    } else if (path[0] == '/') {
        snprintf(full_path, size, "%s%s", VFS_MOUNT_POINT, path);
    } else {
        snprintf(full_path, size, "%s/%s", VFS_MOUNT_POINT, path);
    }
}

/* ============================================================================
 * FILE OPERATIONS
 * ============================================================================ */

bool storage_file_exists(const char *path)
{
    if (!storage_is_mounted() || !path) return false;
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    return vfs_exists(full_path);
}

esp_err_t storage_file_is_empty(const char *path, bool *empty)
{
    if (!empty) return ESP_ERR_INVALID_ARG;
    
    size_t size;
    esp_err_t ret = storage_file_get_size(path, &size);
    if (ret == ESP_OK) {
        *empty = (size == 0);
    }
    return ret;
}

esp_err_t storage_file_get_info(const char *path, storage_file_info_t *info)
{
    if (!storage_is_mounted() || !path || !info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    vfs_stat_t st;
    esp_err_t ret = vfs_stat(full_path, &st);
    if (ret != ESP_OK) {
        return ret;
    }
    
    strncpy(info->path, full_path, sizeof(info->path) - 1);
    info->size = st.size;
    info->modified_time = st.mtime;
    info->created_time = st.ctime;
    info->is_directory = (st.type == VFS_TYPE_DIR);
    info->is_hidden = st.is_hidden;
    info->is_readonly = st.is_readonly;
    
    return ESP_OK;
}

esp_err_t storage_file_get_size(const char *path, size_t *size)
{
    if (!size) return ESP_ERR_INVALID_ARG;
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    return vfs_get_size(full_path, size);
}

esp_err_t storage_file_get_extension(const char *path, char *ext, size_t size)
{
    if (!path || !ext || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) {
        ext[0] = '\0';
        return ESP_OK;
    }
    
    strncpy(ext, dot + 1, size - 1);
    ext[size - 1] = '\0';
    return ESP_OK;
}

esp_err_t storage_file_delete(const char *path)
{
    if (!storage_is_mounted() || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    esp_err_t ret = vfs_unlink(full_path);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "File deleted: %s", full_path);
    }
    return ret;
}

esp_err_t storage_file_rename(const char *old_path, const char *new_path)
{
    if (!storage_is_mounted() || !old_path || !new_path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char old_full[256], new_full[256];
    resolve_path(old_path, old_full, sizeof(old_full));
    resolve_path(new_path, new_full, sizeof(new_full));
    
    esp_err_t ret = vfs_rename(old_full, new_full);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Renamed: %s -> %s", old_full, new_full);
    }
    return ret;
}

esp_err_t storage_file_copy(const char *src, const char *dst)
{
    if (!storage_is_mounted() || !src || !dst) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char src_full[256], dst_full[256];
    resolve_path(src, src_full, sizeof(src_full));
    resolve_path(dst, dst_full, sizeof(dst_full));
    
    esp_err_t ret = vfs_copy_file(src_full, dst_full);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Copied: %s -> %s", src_full, dst_full);
    }
    return ret;
}

esp_err_t storage_file_move(const char *src, const char *dst)
{
    return storage_file_rename(src, dst);
}

esp_err_t storage_file_truncate(const char *path, size_t size)
{
    if (!storage_is_mounted() || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    return vfs_truncate(full_path, size);
}

esp_err_t storage_file_clear(const char *path)
{
    return storage_file_truncate(path, 0);
}

esp_err_t storage_file_compare(const char *path1, const char *path2, bool *equal)
{
    if (!equal) return ESP_ERR_INVALID_ARG;
    *equal = false;
    
    size_t s1, s2;
    if (storage_file_get_size(path1, &s1) != ESP_OK ||
        storage_file_get_size(path2, &s2) != ESP_OK) {
        return ESP_FAIL;
    }
    
    if (s1 != s2) return ESP_OK;
    
    char full1[256], full2[256];
    resolve_path(path1, full1, sizeof(full1));
    resolve_path(path2, full2, sizeof(full2));
    
    vfs_fd_t fd1 = vfs_open(full1, VFS_O_RDONLY, 0);
    vfs_fd_t fd2 = vfs_open(full2, VFS_O_RDONLY, 0);
    
    if (fd1 == VFS_INVALID_FD || fd2 == VFS_INVALID_FD) {
        if (fd1 != VFS_INVALID_FD) vfs_close(fd1);
        if (fd2 != VFS_INVALID_FD) vfs_close(fd2);
        return ESP_FAIL;
    }
    
    uint8_t buf1[256], buf2[256];
    ssize_t r1, r2;
    bool files_equal = true;
    
    while ((r1 = vfs_read(fd1, buf1, sizeof(buf1))) > 0) {
        r2 = vfs_read(fd2, buf2, sizeof(buf2));
        
        if (r1 != r2 || memcmp(buf1, buf2, r1) != 0) {
            files_equal = false;
            break;
        }
    }
    
    vfs_close(fd1);
    vfs_close(fd2);
    
    *equal = files_equal;
    return ESP_OK;
}

/* ============================================================================
 * DIRECTORY OPERATIONS
 * ============================================================================ */

esp_err_t storage_dir_create(const char *path)
{
    if (!storage_is_mounted() || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    esp_err_t ret = vfs_mkdir(full_path, 0755);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Directory created: %s", full_path);
    }
    return ret;
}

esp_err_t storage_dir_remove(const char *path)
{
    if (!storage_is_mounted() || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    return vfs_rmdir(full_path);
}

esp_err_t storage_dir_remove_recursive(const char *path)
{
    if (!storage_is_mounted() || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    return vfs_rmdir_recursive(full_path);
}

bool storage_dir_exists(const char *path)
{
    storage_file_info_t info;
    if (storage_file_get_info(path, &info) == ESP_OK) {
        return info.is_directory;
    }
    return false;
}

esp_err_t storage_dir_is_empty(const char *path, bool *empty)
{
    if (!empty) return ESP_ERR_INVALID_ARG;
    
    uint32_t file_count, dir_count;
    esp_err_t ret = storage_dir_count(path, &file_count, &dir_count);
    if (ret == ESP_OK) {
        *empty = (file_count == 0 && dir_count == 0);
    }
    return ret;
}

esp_err_t storage_dir_list(const char *path, storage_dir_callback_t callback, void *user_data)
{
    if (!storage_is_mounted() || !path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    vfs_dir_t dir = vfs_opendir(full_path);
    if (!dir) {
        return ESP_FAIL;
    }
    
    vfs_stat_t entry;
    while (vfs_readdir(dir, &entry) == ESP_OK) {
        callback(entry.name, (entry.type == VFS_TYPE_DIR), user_data);
    }
    
    vfs_closedir(dir);
    return ESP_OK;
}

esp_err_t storage_dir_count(const char *path, uint32_t *file_count, uint32_t *dir_count)
{
    if (!storage_is_mounted() || !path || !file_count || !dir_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    vfs_dir_t dir = vfs_opendir(full_path);
    if (!dir) {
        return ESP_FAIL;
    }
    
    *file_count = 0;
    *dir_count = 0;
    
    vfs_stat_t entry;
    while (vfs_readdir(dir, &entry) == ESP_OK) {
        if (entry.type == VFS_TYPE_DIR) {
            (*dir_count)++;
        } else {
            (*file_count)++;
        }
    }
    
    vfs_closedir(dir);
    return ESP_OK;
}

esp_err_t storage_dir_copy_recursive(const char *src, const char *dst)
{
    ESP_LOGW(TAG, "storage_dir_copy_recursive not fully implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t storage_dir_get_size(const char *path, uint64_t *total_size)
{
    ESP_LOGW(TAG, "storage_dir_get_size not fully implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

/* ============================================================================
 * STORAGE INFO
 * ============================================================================ */

esp_err_t storage_get_info(storage_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    
    memset(info, 0, sizeof(storage_info_t));
    
    strncpy(info->backend_name, VFS_BACKEND_NAME, sizeof(info->backend_name) - 1);
    strncpy(info->mount_point, VFS_MOUNT_POINT, sizeof(info->mount_point) - 1);
    info->is_mounted = storage_is_mounted();
    
    if (info->is_mounted) {
        vfs_statvfs_t stat;
        if (vfs_statvfs(VFS_MOUNT_POINT, &stat) == ESP_OK) {
            info->total_bytes = stat.total_bytes;
            info->free_bytes = stat.free_bytes;
            info->used_bytes = stat.used_bytes;
            info->block_size = stat.block_size;
        }
    }
    
    return ESP_OK;
}

void storage_print_info_detailed(void)
{
    storage_info_t info;
    if (storage_get_info(&info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get storage info");
        return;
    }
    
    ESP_LOGI(TAG, "========== Storage Info ==========");
    ESP_LOGI(TAG, "Backend: %s", info.backend_name);
    ESP_LOGI(TAG, "Mount Point: %s", info.mount_point);
    ESP_LOGI(TAG, "Status: %s", info.is_mounted ? "Mounted" : "Unmounted");
    
    if (info.is_mounted) {
        ESP_LOGI(TAG, "Total: %" PRIu64 " KB (%.2f MB)", 
            info.total_bytes / 1024, (float)info.total_bytes / (1024 * 1024));
        ESP_LOGI(TAG, "Free: %" PRIu64 " KB (%.2f MB)", 
            info.free_bytes / 1024, (float)info.free_bytes / (1024 * 1024));
        ESP_LOGI(TAG, "Used: %" PRIu64 " KB (%.2f MB)", 
            info.used_bytes / 1024, (float)info.used_bytes / (1024 * 1024));
        
        float usage = info.total_bytes > 0 ?
            ((float)info.used_bytes / info.total_bytes) * 100.0f : 0.0f;
        ESP_LOGI(TAG, "Usage: %.1f%%", usage);
        ESP_LOGI(TAG, "Block size: %" PRIu32 " bytes", info.block_size);
    }
    
    ESP_LOGI(TAG, "==================================");
}

esp_err_t storage_get_free_space(uint64_t *free_bytes)
{
    if (!free_bytes) return ESP_ERR_INVALID_ARG;
    return vfs_get_free_space(VFS_MOUNT_POINT, free_bytes);
}

esp_err_t storage_get_total_space(uint64_t *total_bytes)
{
    if (!total_bytes) return ESP_ERR_INVALID_ARG;
    return vfs_get_total_space(VFS_MOUNT_POINT, total_bytes);
}

esp_err_t storage_get_used_space(uint64_t *used_bytes)
{
    if (!used_bytes) return ESP_ERR_INVALID_ARG;
    
    vfs_statvfs_t stat;
    esp_err_t ret = vfs_statvfs(VFS_MOUNT_POINT, &stat);
    if (ret == ESP_OK) {
        *used_bytes = stat.used_bytes;
    }
    return ret;
}

esp_err_t storage_get_usage_percent(float *percentage)
{
    if (!percentage) return ESP_ERR_INVALID_ARG;
    return vfs_get_usage_percent(VFS_MOUNT_POINT, percentage);
}

const char* storage_get_backend_type(void)
{
    return VFS_BACKEND_NAME;
}

const char* storage_get_mount_point_str(void)
{
    return VFS_MOUNT_POINT;
}