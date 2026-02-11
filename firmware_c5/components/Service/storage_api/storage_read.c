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
 * @file storage_read.c
 * @brief Read operations implementation
 */

#include "storage_read.h"
#include "storage_init.h"
#include "vfs_config.h"
#include "vfs_core.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "storage_read";
#define MAX_LINE_LEN 512

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
 * STRING READ
 * ============================================================================ */

esp_err_t storage_read_string(const char *path, char *buffer, size_t buffer_size)
{
    if (!storage_is_mounted()) {
        ESP_LOGE(TAG, "Storage not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    size_t bytes_read;
    esp_err_t ret = vfs_read_file(full_path, buffer, buffer_size - 1, &bytes_read);
    
    if (ret == ESP_OK) {
        buffer[bytes_read] = '\0';
        ESP_LOGI(TAG, "String read: %s (%zu bytes)", full_path, bytes_read);
    } else {
        ESP_LOGE(TAG, "Failed to read: %s", full_path);
    }
    
    return ret;
}

/* ============================================================================
 * BINARY READ
 * ============================================================================ */

esp_err_t storage_read_binary(const char *path, void *buffer, size_t size, size_t *bytes_read)
{
    if (!storage_is_mounted()) {
        ESP_LOGE(TAG, "Storage not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !buffer) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    esp_err_t ret = vfs_read_file(full_path, buffer, size, bytes_read);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Binary read: %s (%zu bytes)", full_path, bytes_read ? *bytes_read : size);
    } else {
        ESP_LOGE(TAG, "Failed to read binary: %s", full_path);
    }
    
    return ret;
}

/* ============================================================================
 * LINE READ
 * ============================================================================ */

esp_err_t storage_read_line(const char *path, char *buffer, size_t buffer_size, uint32_t line_number)
{
    if (!storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !buffer || buffer_size == 0 || line_number == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    vfs_fd_t fd = vfs_open(full_path, VFS_O_RDONLY, 0);
    if (fd == VFS_INVALID_FD) {
        ESP_LOGE(TAG, "Failed to open: %s", full_path);
        return ESP_FAIL;
    }
    
    uint32_t current_line = 0;
    char line_buf[MAX_LINE_LEN];
    size_t pos = 0;
    char c;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    while (vfs_read(fd, &c, 1) == 1) {
        if (c == '\n' || pos >= MAX_LINE_LEN - 1) {
            line_buf[pos] = '\0';
            current_line++;
            
            if (current_line == line_number) {
                strncpy(buffer, line_buf, buffer_size - 1);
                buffer[buffer_size - 1] = '\0';
                ret = ESP_OK;
                break;
            }
            
            pos = 0;
        } else {
            line_buf[pos++] = c;
        }
    }
    
    vfs_close(fd);
    return ret;
}

esp_err_t storage_read_first_line(const char *path, char *buffer, size_t buffer_size)
{
    return storage_read_line(path, buffer, buffer_size, 1);
}

esp_err_t storage_read_last_line(const char *path, char *buffer, size_t buffer_size)
{
    if (!storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    vfs_fd_t fd = vfs_open(full_path, VFS_O_RDONLY, 0);
    if (fd == VFS_INVALID_FD) {
        return ESP_FAIL;
    }
    
    char last_line[MAX_LINE_LEN] = {0};
    char current_line[MAX_LINE_LEN];
    size_t pos = 0;
    char c;
    
    while (vfs_read(fd, &c, 1) == 1) {
        if (c == '\n' || pos >= MAX_LINE_LEN - 1) {
            current_line[pos] = '\0';
            if (pos > 0) {
                strncpy(last_line, current_line, sizeof(last_line) - 1);
            }
            pos = 0;
        } else {
            current_line[pos++] = c;
        }
    }
    
    if (pos > 0) {
        current_line[pos] = '\0';
        strncpy(last_line, current_line, sizeof(last_line) - 1);
    }
    
    vfs_close(fd);
    
    if (last_line[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }
    
    strncpy(buffer, last_line, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    
    return ESP_OK;
}

esp_err_t storage_read_lines(const char *path, storage_line_callback_t callback, void *user_data)
{
    if (!storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    vfs_fd_t fd = vfs_open(full_path, VFS_O_RDONLY, 0);
    if (fd == VFS_INVALID_FD) {
        return ESP_FAIL;
    }
    
    char line[MAX_LINE_LEN];
    size_t pos = 0;
    char c;
    
    while (vfs_read(fd, &c, 1) == 1) {
        if (c == '\n' || pos >= MAX_LINE_LEN - 1) {
            line[pos] = '\0';
            callback(line, user_data);
            pos = 0;
        } else {
            line[pos++] = c;
        }
    }
    
    if (pos > 0) {
        line[pos] = '\0';
        callback(line, user_data);
    }
    
    vfs_close(fd);
    return ESP_OK;
}

esp_err_t storage_count_lines(const char *path, uint32_t *line_count)
{
    if (!storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !line_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    vfs_fd_t fd = vfs_open(full_path, VFS_O_RDONLY, 0);
    if (fd == VFS_INVALID_FD) {
        return ESP_FAIL;
    }
    
    *line_count = 0;
    char c;
    
    while (vfs_read(fd, &c, 1) == 1) {
        if (c == '\n') {
            (*line_count)++;
        }
    }
    
    vfs_close(fd);
    return ESP_OK;
}

/* ============================================================================
 * CHUNK READ
 * ============================================================================ */

esp_err_t storage_read_chunk(const char *path, size_t offset, void *buffer, size_t size, size_t *bytes_read)
{
    if (!storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !buffer) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    vfs_fd_t fd = vfs_open(full_path, VFS_O_RDONLY, 0);
    if (fd == VFS_INVALID_FD) {
        return ESP_FAIL;
    }
    
    if (vfs_lseek(fd, offset, VFS_SEEK_SET) < 0) {
        vfs_close(fd);
        return ESP_FAIL;
    }
    
    ssize_t read = vfs_read(fd, buffer, size);
    vfs_close(fd);
    
    if (read < 0) {
        return ESP_FAIL;
    }
    
    if (bytes_read) {
        *bytes_read = read;
    }
    
    return ESP_OK;
}

/* ============================================================================
 * SPECIFIC TYPES
 * ============================================================================ */

esp_err_t storage_read_int(const char *path, int32_t *value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char buffer[32];
    esp_err_t ret = storage_read_string(path, buffer, sizeof(buffer));
    if (ret == ESP_OK) {
        *value = atoi(buffer);
    }
    return ret;
}

esp_err_t storage_read_float(const char *path, float *value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char buffer[32];
    esp_err_t ret = storage_read_string(path, buffer, sizeof(buffer));
    if (ret == ESP_OK) {
        *value = atof(buffer);
    }
    return ret;
}

esp_err_t storage_read_bytes(const char *path, uint8_t *bytes, size_t max_count, size_t *count)
{
    return storage_read_binary(path, bytes, max_count, count);
}

esp_err_t storage_read_byte(const char *path, uint8_t *byte)
{
    if (!byte) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t read;
    return storage_read_binary(path, byte, 1, &read);
}

/* ============================================================================
 * SEARCH
 * ============================================================================ */

esp_err_t storage_file_contains(const char *path, const char *search, bool *found)
{
    if (!storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !search || !found) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    vfs_fd_t fd = vfs_open(full_path, VFS_O_RDONLY, 0);
    if (fd == VFS_INVALID_FD) {
        return ESP_FAIL;
    }
    
    *found = false;
    char line[MAX_LINE_LEN];
    size_t pos = 0;
    char c;
    
    while (vfs_read(fd, &c, 1) == 1) {
        if (c == '\n' || pos >= MAX_LINE_LEN - 1) {
            line[pos] = '\0';
            if (strstr(line, search) != NULL) {
                *found = true;
                break;
            }
            pos = 0;
        } else {
            line[pos++] = c;
        }
    }
    
    vfs_close(fd);
    return ESP_OK;
}

esp_err_t storage_count_occurrences(const char *path, const char *search, uint32_t *count)
{
    if (!storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !search || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    vfs_fd_t fd = vfs_open(full_path, VFS_O_RDONLY, 0);
    if (fd == VFS_INVALID_FD) {
        return ESP_FAIL;
    }
    
    *count = 0;
    char line[MAX_LINE_LEN];
    size_t pos = 0;
    char c;
    
    while (vfs_read(fd, &c, 1) == 1) {
        if (c == '\n' || pos >= MAX_LINE_LEN - 1) {
            line[pos] = '\0';
            
            char *p = line;
            while ((p = strstr(p, search)) != NULL) {
                (*count)++;
                p += strlen(search);
            }
            
            pos = 0;
        } else {
            line[pos++] = c;
        }
    }
    
    vfs_close(fd);
    return ESP_OK;
}