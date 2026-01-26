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

#include "storage_assets.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "storage_assets";

#define ASSETS_MOUNT_POINT "/assets"
#define ASSETS_PARTITION_LABEL "assets"

static bool s_initialized = false;

static void list_directory_recursive(const char *path, const char *prefix, int *file_count, int *dir_count)
{
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return;
    }

    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char filepath[512];
        int len = snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        if (len >= sizeof(filepath)) {
            ESP_LOGW(TAG, "%s[SKIP] %s (path too long)", prefix, entry->d_name);
            continue;
        }
        
        struct stat st;
        if (stat(filepath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                (*dir_count)++;
                ESP_LOGI(TAG, "%s[DIR] %s/", prefix, entry->d_name);
                
                // Recursive call for subdirectories
                char new_prefix[128];
                snprintf(new_prefix, sizeof(new_prefix), "%s  ", prefix);
                list_directory_recursive(filepath, new_prefix, file_count, dir_count);
            } else if (S_ISREG(st.st_mode)) {
                (*file_count)++;
                ESP_LOGI(TAG, "%s[%d] %s (%ld bytes)", prefix, *file_count, entry->d_name, st.st_size);
            }
        }
    }
    
    closedir(dir);
}

static void storage_assets_list_files(void)
{
    ESP_LOGI(TAG, "=== Files in assets partition ===");
    
    int file_count = 0;
    int dir_count = 0;
    
    list_directory_recursive(ASSETS_MOUNT_POINT, "  ", &file_count, &dir_count);
    
    if (file_count == 0 && dir_count == 0) {
        ESP_LOGW(TAG, "  (empty - partition has no files!)");
        ESP_LOGW(TAG, "  Make sure to flash the assets partition:");
        ESP_LOGW(TAG, "  > idf.py flash");
        ESP_LOGW(TAG, "  Or manually:");
        ESP_LOGW(TAG, "  > esptool.py write_flash 0xYOUR_OFFSET build/assets.bin");
    } else {
        ESP_LOGI(TAG, "Total: %d file(s), %d dir(s)", file_count, dir_count);
    }
    ESP_LOGI(TAG, "================================");
}

esp_err_t storage_assets_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing LittleFS for assets partition");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = ASSETS_MOUNT_POINT,
        .partition_label = ASSETS_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Partition '%s' not found", ASSETS_PARTITION_LABEL);
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(ASSETS_PARTITION_LABEL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get partition info: %s", esp_err_to_name(ret));
        esp_vfs_littlefs_unregister(ASSETS_PARTITION_LABEL);
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Assets ready at %s", ASSETS_MOUNT_POINT);
    ESP_LOGI(TAG, "Partition size: %d bytes, used: %d bytes", total, used);
    
    // List files in the partition
    storage_assets_list_files();
    
    return ESP_OK;
}

esp_err_t storage_assets_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing assets partition");

    esp_err_t ret = esp_vfs_littlefs_unregister(ASSETS_PARTITION_LABEL);
    if (ret == ESP_OK) {
        s_initialized = false;
    }
    
    return ret;
}

bool storage_assets_is_mounted(void)
{
    return s_initialized;
}

esp_err_t storage_assets_get_file_size(const char *filename, size_t *out_size)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Assets not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (filename == NULL || out_size == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s", ASSETS_MOUNT_POINT, filename);

    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    if (size < 0) {
        ESP_LOGE(TAG, "Failed to get file size");
        return ESP_FAIL;
    }

    *out_size = (size_t)size;
    return ESP_OK;
}

esp_err_t storage_assets_read_file(const char *filename, uint8_t *buffer, size_t size, size_t *out_read)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Assets not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (filename == NULL || buffer == NULL || size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s", ASSETS_MOUNT_POINT, filename);

    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }

    size_t bytes_read = fread(buffer, 1, size, f);
    fclose(f);

    if (out_read != NULL) {
        *out_read = bytes_read;
    }

    ESP_LOGI(TAG, "Read %d bytes from %s", bytes_read, filename);
    return ESP_OK;
}

uint8_t* storage_assets_load_file(const char *filename, size_t *out_size)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Assets not initialized");
        return NULL;
    }

    size_t file_size;
    esp_err_t ret = storage_assets_get_file_size(filename, &file_size);
    if (ret != ESP_OK) {
        return NULL;
    }

    uint8_t *buffer = (uint8_t*)malloc(file_size);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes for %s", file_size, filename);
        return NULL;
    }

    size_t bytes_read;
    ret = storage_assets_read_file(filename, buffer, file_size, &bytes_read);
    if (ret != ESP_OK || bytes_read != file_size) {
        ESP_LOGE(TAG, "Failed to read complete file");
        free(buffer);
        return NULL;
    }

    if (out_size != NULL) {
        *out_size = file_size;
    }

    ESP_LOGI(TAG, "Loaded %s (%d bytes)", filename, file_size);
    return buffer;
}

const char* storage_assets_get_mount_point(void)
{
    return ASSETS_MOUNT_POINT;
}

void storage_assets_print_info(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "Assets not initialized");
        return;
    }

    size_t total = 0, used = 0;
    esp_err_t ret = esp_littlefs_info(ASSETS_PARTITION_LABEL, &total, &used);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get partition info: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "=== Assets Partition Info ===");
    ESP_LOGI(TAG, "Mount point: %s", ASSETS_MOUNT_POINT);
    ESP_LOGI(TAG, "Partition: %s", ASSETS_PARTITION_LABEL);
    ESP_LOGI(TAG, "Total size: %d bytes (%.2f KB)", total, total / 1024.0);
    ESP_LOGI(TAG, "Used: %d bytes (%.2f KB)", used, used / 1024.0);
    ESP_LOGI(TAG, "Free: %d bytes (%.2f KB)", total - used, (total - used) / 1024.0);
    ESP_LOGI(TAG, "Usage: %.1f%%", (used * 100.0) / total);
}

esp_err_t storage_assets_write_file(const char *filename, const char *data) {
    if (!s_initialized) {
        ESP_LOGE(TAG, "Assets not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s", ASSETS_MOUNT_POINT, filename);

    FILE *f = fopen(filepath, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        return ESP_FAIL;
    }

    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Failed to write complete file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File %s updated in assets partition", filename);
    return ESP_OK;
}