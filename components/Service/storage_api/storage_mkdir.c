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

static const char *TAG = "storage";

esp_err_t storage_mkdir_recursive(const char *path)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char temp_path[256];
    strncpy(temp_path, path, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';
    
    char *last_slash = strrchr(temp_path, '/');
    if (!last_slash || last_slash == temp_path) {
        return ESP_OK;
    }
    *last_slash = '\0';
    
    char build_path[256] = "";
    char path_copy[256];
    strncpy(path_copy, temp_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    char *token;
    char *saveptr;
    token = strtok_r(path_copy, "/", &saveptr);
    
    while (token != NULL) {
        if (strlen(build_path) > 0) {
            strcat(build_path, "/");
        } else if (temp_path[0] == '/') {
            strcat(build_path, "/");
        }
        strcat(build_path, token);
        
        struct stat st;
        if (stat(build_path, &st) != 0) {
            if (mkdir(build_path, 0755) != 0 && errno != EEXIST) {
                ESP_LOGE(TAG, "mkdir failed: %s", build_path);
                return ESP_FAIL;
            }
        } else if (!S_ISDIR(st.st_mode)) {
            ESP_LOGE(TAG, "Not a directory: %s", build_path);
            return ESP_FAIL;
        }
        
        token = strtok_r(NULL, "/", &saveptr);
    }
    
    return ESP_OK;
}