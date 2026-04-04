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

#include "storage_mkdir.h"
#include "esp_log.h"
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "storage_mkdir";

esp_err_t storage_mkdir_recursive(const char *path)
{
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Creating directory path: %s", path);
    
    char temp_path[256];
    strncpy(temp_path, path, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';
    
    // Remove trailing slash if present
    size_t len = strlen(temp_path);
    if (len > 1 && temp_path[len - 1] == '/') {
        temp_path[len - 1] = '\0';
    }
    
    char build_path[256] = "";
    char path_copy[256];
    strncpy(path_copy, temp_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    char *token;
    char *saveptr;
    bool is_absolute = (temp_path[0] == '/');
    
    token = strtok_r(path_copy, "/", &saveptr);
    
    while (token != NULL) {
        // Build the path progressively
        if (strlen(build_path) > 0) {
            strcat(build_path, "/");
        } else if (is_absolute) {
            strcat(build_path, "/");
        }
        strcat(build_path, token);
        
        struct stat st;
        if (stat(build_path, &st) != 0) {
            // Directory doesn't exist, create it
            ESP_LOGI(TAG, "Creating directory: %s", build_path);
            if (mkdir(build_path, 0755) != 0) {
                if (errno == EEXIST) {
                    ESP_LOGD(TAG, "Directory already exists: %s", build_path);
                } else {
                    ESP_LOGE(TAG, "Failed to create directory: %s (errno: %d - %s)", 
                             build_path, errno, strerror(errno));
                    return ESP_FAIL;
                }
            } else {
                ESP_LOGI(TAG, "Successfully created: %s", build_path);
            }
        } else {
            // Path exists, check if it's a directory
            if (!S_ISDIR(st.st_mode)) {
                ESP_LOGE(TAG, "Path exists but is not a directory: %s", build_path);
                return ESP_FAIL;
            }
            ESP_LOGD(TAG, "Directory already exists: %s", build_path);
        }
        
        token = strtok_r(NULL, "/", &saveptr);
    }
    
    ESP_LOGD(TAG, "Successfully processed path: %s", path);
    return ESP_OK;
}