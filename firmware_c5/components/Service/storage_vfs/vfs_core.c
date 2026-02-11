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

#include "vfs_core.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "vfs_core";

typedef struct {
    bool in_use;
    vfs_fd_t native_fd;
    const vfs_backend_config_t *backend;
    char path[VFS_MAX_PATH];
    int flags;
} vfs_file_descriptor_t;

struct vfs_dir_s {
    vfs_dir_t native_dir;
    const vfs_backend_config_t *backend;
    char path[VFS_MAX_PATH];
};

static struct {
    vfs_backend_config_t backends[VFS_MAX_BACKENDS];
    size_t count;
} s_vfs_backends = {0};

#define MAX_OPEN_FILES 32
static vfs_file_descriptor_t s_fd_table[MAX_OPEN_FILES] = {0};

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

static const vfs_backend_config_t* find_backend_by_mount(const char *path)
{
    if (!path) return NULL;
    
    const vfs_backend_config_t *best_match = NULL;
    size_t best_len = 0;
    
    for (size_t i = 0; i < s_vfs_backends.count; i++) {
        const char *mount = s_vfs_backends.backends[i].mount_point;
        size_t mount_len = strlen(mount);
        
        if (strncmp(path, mount, mount_len) == 0) {
            if (mount_len > best_len) {
                best_match = &s_vfs_backends.backends[i];
                best_len = mount_len;
            }
        }
    }
    
    return best_match;
}

static int alloc_fd(void)
{
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!s_fd_table[i].in_use) {
            s_fd_table[i].in_use = true;
            return i;
        }
    }
    return VFS_INVALID_FD;
}

static void free_fd(int fd)
{
    if (fd >= 0 && fd < MAX_OPEN_FILES) {
        memset(&s_fd_table[fd], 0, sizeof(vfs_file_descriptor_t));
    }
}

static vfs_file_descriptor_t* get_fd(int fd)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES || !s_fd_table[fd].in_use) {
        return NULL;
    }
    return &s_fd_table[fd];
}

/* ============================================================================
 * BACKEND MANAGEMENT
 * ============================================================================ */

esp_err_t vfs_register_backend(const vfs_backend_config_t *config)
{
    if (!config || !config->mount_point || !config->ops) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_vfs_backends.count >= VFS_MAX_BACKENDS) {
        ESP_LOGE(TAG, "Backend limit reached");
        return ESP_ERR_NO_MEM;
    }
    
    for (size_t i = 0; i < s_vfs_backends.count; i++) {
        if (strcmp(s_vfs_backends.backends[i].mount_point, config->mount_point) == 0) {
            ESP_LOGW(TAG, "Mount point already registered: %s", config->mount_point);
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    memcpy(&s_vfs_backends.backends[s_vfs_backends.count], config, sizeof(vfs_backend_config_t));
    s_vfs_backends.count++;
    
    ESP_LOGI(TAG, "Backend registered: %s (type: %d)", config->mount_point, config->type);
    return ESP_OK;
}

esp_err_t vfs_unregister_backend(const char *mount_point)
{
    if (!mount_point) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (size_t i = 0; i < s_vfs_backends.count; i++) {
        if (strcmp(s_vfs_backends.backends[i].mount_point, mount_point) == 0) {
            for (size_t j = i; j < s_vfs_backends.count - 1; j++) {
                s_vfs_backends.backends[j] = s_vfs_backends.backends[j + 1];
            }
            s_vfs_backends.count--;
            
            ESP_LOGI(TAG, "Backend unregistered: %s", mount_point);
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "Backend not found: %s", mount_point);
    return ESP_ERR_NOT_FOUND;
}

const vfs_backend_config_t* vfs_get_backend(const char *path)
{
    return find_backend_by_mount(path);
}

size_t vfs_list_backends(const vfs_backend_config_t **backends, size_t max_count)
{
    if (!backends) return 0;
    
    size_t count = s_vfs_backends.count < max_count ? s_vfs_backends.count : max_count;
    for (size_t i = 0; i < count; i++) {
        backends[i] = &s_vfs_backends.backends[i];
    }
    
    return count;
}

/* ============================================================================
 * FILE OPERATIONS - SIMPLIFIED (NO FLAG CONVERSION)
 * ============================================================================ */

vfs_fd_t vfs_open(const char *path, int flags, int mode)
{
    if (!path) {
        ESP_LOGE(TAG, "Null path");
        return VFS_INVALID_FD;
    }
    
    const vfs_backend_config_t *backend = find_backend_by_mount(path);
    if (!backend || !backend->ops || !backend->ops->open) {
        ESP_LOGE(TAG, "Backend not found for: %s", path);
        return VFS_INVALID_FD;
    }
    
    int fd = alloc_fd();
    if (fd == VFS_INVALID_FD) {
        ESP_LOGE(TAG, "No file descriptors available");
        return VFS_INVALID_FD;
    }
    
    // NO CONVERSION - pass flags directly as they are already POSIX
    vfs_fd_t native_fd = backend->ops->open(path, flags, mode);
    
    if (native_fd == VFS_INVALID_FD) {
        ESP_LOGE(TAG, "Backend open failed: %s", path);
        free_fd(fd);
        return VFS_INVALID_FD;
    }
    
    s_fd_table[fd].native_fd = native_fd;
    s_fd_table[fd].backend = backend;
    s_fd_table[fd].flags = flags;
    strncpy(s_fd_table[fd].path, path, VFS_MAX_PATH - 1);
    s_fd_table[fd].path[VFS_MAX_PATH - 1] = '\0';
    
    return fd;
}

ssize_t vfs_read(vfs_fd_t fd, void *buf, size_t size)
{
    vfs_file_descriptor_t *vfd = get_fd(fd);
    if (!vfd || !vfd->backend->ops->read) {
        ESP_LOGE(TAG, "Invalid FD: %d", fd);
        return -1;
    }
    
    return vfd->backend->ops->read(vfd->native_fd, buf, size);
}

ssize_t vfs_write(vfs_fd_t fd, const void *buf, size_t size)
{
    vfs_file_descriptor_t *vfd = get_fd(fd);
    if (!vfd || !vfd->backend->ops->write) {
        ESP_LOGE(TAG, "Invalid FD: %d", fd);
        return -1;
    }
    
    return vfd->backend->ops->write(vfd->native_fd, buf, size);
}

off_t vfs_lseek(vfs_fd_t fd, off_t offset, int whence)
{
    vfs_file_descriptor_t *vfd = get_fd(fd);
    if (!vfd || !vfd->backend->ops->lseek) {
        ESP_LOGE(TAG, "Invalid FD: %d", fd);
        return -1;
    }
    
    return vfd->backend->ops->lseek(vfd->native_fd, offset, whence);
}

esp_err_t vfs_close(vfs_fd_t fd)
{
    vfs_file_descriptor_t *vfd = get_fd(fd);
    if (!vfd) {
        ESP_LOGE(TAG, "Invalid FD: %d", fd);
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    if (vfd->backend->ops->close) {
        ret = vfd->backend->ops->close(vfd->native_fd);
    }
    
    free_fd(fd);
    return ret;
}

esp_err_t vfs_fsync(vfs_fd_t fd)
{
    vfs_file_descriptor_t *vfd = get_fd(fd);
    if (!vfd) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (vfd->backend->ops->fsync) {
        return vfd->backend->ops->fsync(vfd->native_fd);
    }
    
    return ESP_OK;
}

/* ============================================================================
 * METADATA
 * ============================================================================ */

esp_err_t vfs_stat(const char *path, vfs_stat_t *st)
{
    if (!path || !st) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const vfs_backend_config_t *backend = find_backend_by_mount(path);
    if (!backend || !backend->ops->stat) {
        ESP_LOGE(TAG, "Backend not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    return backend->ops->stat(path, st);
}

esp_err_t vfs_fstat(vfs_fd_t fd, vfs_stat_t *st)
{
    if (!st) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vfs_file_descriptor_t *vfd = get_fd(fd);
    if (!vfd || !vfd->backend->ops->fstat) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return vfd->backend->ops->fstat(vfd->native_fd, st);
}

esp_err_t vfs_rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const vfs_backend_config_t *backend = find_backend_by_mount(old_path);
    if (!backend || !backend->ops->rename) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return backend->ops->rename(old_path, new_path);
}

esp_err_t vfs_unlink(const char *path)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const vfs_backend_config_t *backend = find_backend_by_mount(path);
    if (!backend || !backend->ops->unlink) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return backend->ops->unlink(path);
}

esp_err_t vfs_truncate(const char *path, off_t length)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const vfs_backend_config_t *backend = find_backend_by_mount(path);
    if (!backend || !backend->ops->truncate) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return backend->ops->truncate(path, length);
}

bool vfs_exists(const char *path)
{
    vfs_stat_t st;
    return (vfs_stat(path, &st) == ESP_OK);
}

/* ============================================================================
 * DIRECTORIES
 * ============================================================================ */

esp_err_t vfs_mkdir(const char *path, int mode)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const vfs_backend_config_t *backend = find_backend_by_mount(path);
    if (!backend || !backend->ops->mkdir) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return backend->ops->mkdir(path, mode);
}

esp_err_t vfs_rmdir(const char *path)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const vfs_backend_config_t *backend = find_backend_by_mount(path);
    if (!backend || !backend->ops->rmdir) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return backend->ops->rmdir(path);
}

esp_err_t vfs_rmdir_recursive(const char *path)
{
    vfs_dir_t dir = vfs_opendir(path);
    if (!dir) {
        return ESP_FAIL;
    }
    
    vfs_stat_t entry;
    while (vfs_readdir(dir, &entry) == ESP_OK) {
        char full_path[VFS_MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry.name);
        
        if (entry.type == VFS_TYPE_DIR) {
            vfs_rmdir_recursive(full_path);
        } else {
            vfs_unlink(full_path);
        }
    }
    
    vfs_closedir(dir);
    return vfs_rmdir(path);
}

vfs_dir_t vfs_opendir(const char *path)
{
    if (!path) {
        return NULL;
    }
    
    const vfs_backend_config_t *backend = find_backend_by_mount(path);
    if (!backend || !backend->ops->opendir) {
        ESP_LOGE(TAG, "Backend does not support opendir: %s", path);
        return NULL;
    }
    
    vfs_dir_t native_dir = backend->ops->opendir(path);
    if (!native_dir) {
        return NULL;
    }
    
    struct vfs_dir_s *dir = malloc(sizeof(struct vfs_dir_s));
    if (!dir) {
        backend->ops->closedir(native_dir);
        return NULL;
    }
    
    dir->native_dir = native_dir;
    dir->backend = backend;
    strncpy(dir->path, path, VFS_MAX_PATH - 1);
    dir->path[VFS_MAX_PATH - 1] = '\0';
    
    return dir;
}

esp_err_t vfs_readdir(vfs_dir_t dir, vfs_stat_t *entry)
{
    if (!dir || !entry || !dir->backend->ops->readdir) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return dir->backend->ops->readdir(dir->native_dir, entry);
}

esp_err_t vfs_closedir(vfs_dir_t dir)
{
    if (!dir) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    if (dir->backend->ops->closedir) {
        ret = dir->backend->ops->closedir(dir->native_dir);
    }
    
    free(dir);
    return ret;
}

esp_err_t vfs_list_dir(const char *path, vfs_dir_callback_t callback, void *user_data)
{
    if (!path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vfs_dir_t dir = vfs_opendir(path);
    if (!dir) {
        return ESP_FAIL;
    }
    
    vfs_stat_t entry;
    while (vfs_readdir(dir, &entry) == ESP_OK) {
        callback(&entry, user_data);
    }
    
    return vfs_closedir(dir);
}

/* ============================================================================
 * FILESYSTEM INFO
 * ============================================================================ */

esp_err_t vfs_statvfs(const char *path, vfs_statvfs_t *stat)
{
    if (!path || !stat) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const vfs_backend_config_t *backend = find_backend_by_mount(path);
    if (!backend || !backend->ops->statvfs) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return backend->ops->statvfs(stat);
}

esp_err_t vfs_get_free_space(const char *path, uint64_t *free_bytes)
{
    if (!free_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vfs_statvfs_t stat;
    esp_err_t ret = vfs_statvfs(path, &stat);
    if (ret == ESP_OK) {
        *free_bytes = stat.free_bytes;
    }
    return ret;
}

esp_err_t vfs_get_total_space(const char *path, uint64_t *total_bytes)
{
    if (!total_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vfs_statvfs_t stat;
    esp_err_t ret = vfs_statvfs(path, &stat);
    if (ret == ESP_OK) {
        *total_bytes = stat.total_bytes;
    }
    return ret;
}

esp_err_t vfs_get_usage_percent(const char *path, float *percentage)
{
    if (!percentage) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vfs_statvfs_t stat;
    esp_err_t ret = vfs_statvfs(path, &stat);
    if (ret == ESP_OK) {
        *percentage = stat.total_bytes > 0 ?
            ((float)stat.used_bytes / stat.total_bytes) * 100.0f : 0.0f;
    }
    return ret;
}

/* ============================================================================
 * HIGH-LEVEL HELPERS
 * ============================================================================ */

esp_err_t vfs_read_file(const char *path, void *buf, size_t size, size_t *bytes_read)
{
    if (!path || !buf) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vfs_fd_t fd = vfs_open(path, VFS_O_RDONLY, 0);
    if (fd == VFS_INVALID_FD) {
        return ESP_FAIL;
    }
    
    ssize_t read = vfs_read(fd, buf, size);
    vfs_close(fd);
    
    if (read < 0) {
        return ESP_FAIL;
    }
    
    if (bytes_read) {
        *bytes_read = read;
    }
    
    return ESP_OK;
}

esp_err_t vfs_write_file(const char *path, const void *buf, size_t size)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (size > 0 && !buf) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vfs_fd_t fd = vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, 0644);
    if (fd == VFS_INVALID_FD) {
        ESP_LOGE(TAG, "Failed to open for write: %s", path);
        return ESP_FAIL;
    }
    
    esp_err_t result = ESP_OK;
    
    if (size > 0) {
        ssize_t written = vfs_write(fd, buf, size);
        if (written != (ssize_t)size) {
            ESP_LOGE(TAG, "Write incomplete: %d of %zu bytes", (int)written, size);
            result = ESP_FAIL;
        }
    }
    
    vfs_fsync(fd);
    vfs_close(fd);
    return result;
}

esp_err_t vfs_append_file(const char *path, const void *buf, size_t size)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (size > 0 && !buf) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vfs_fd_t fd = vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND, 0644);
    if (fd == VFS_INVALID_FD) {
        ESP_LOGE(TAG, "Failed to open for append: %s", path);
        return ESP_FAIL;
    }
    
    esp_err_t result = ESP_OK;
    
    if (size > 0) {
        ssize_t written = vfs_write(fd, buf, size);
        if (written != (ssize_t)size) {
            ESP_LOGE(TAG, "Append incomplete: %d of %zu bytes", (int)written, size);
            result = ESP_FAIL;
        }
    }
    
    vfs_fsync(fd);
    vfs_close(fd);
    return result;
}

esp_err_t vfs_copy_file(const char *src, const char *dst)
{
    if (!src || !dst) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vfs_fd_t fd_src = vfs_open(src, VFS_O_RDONLY, 0);
    if (fd_src == VFS_INVALID_FD) {
        ESP_LOGE(TAG, "Failed to open source: %s", src);
        return ESP_FAIL;
    }
    
    vfs_fd_t fd_dst = vfs_open(dst, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, 0644);
    if (fd_dst == VFS_INVALID_FD) {
        vfs_close(fd_src);
        ESP_LOGE(TAG, "Failed to open destination: %s", dst);
        return ESP_FAIL;
    }
    
    uint8_t buffer[512];
    ssize_t read_bytes;
    esp_err_t ret = ESP_OK;
    
    while ((read_bytes = vfs_read(fd_src, buffer, sizeof(buffer))) > 0) {
        ssize_t written = vfs_write(fd_dst, buffer, read_bytes);
        if (written != read_bytes) {
            ESP_LOGE(TAG, "Copy write failed: %d of %d bytes", (int)written, (int)read_bytes);
            ret = ESP_FAIL;
            break;
        }
    }
    
    if (ret == ESP_OK) {
        vfs_fsync(fd_dst);
    }
    
    vfs_close(fd_src);
    vfs_close(fd_dst);
    
    return ret;
}

esp_err_t vfs_get_size(const char *path, size_t *size)
{
    if (!path || !size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vfs_stat_t st;
    esp_err_t ret = vfs_stat(path, &st);
    if (ret == ESP_OK) {
        *size = st.size;
    }
    return ret;
}