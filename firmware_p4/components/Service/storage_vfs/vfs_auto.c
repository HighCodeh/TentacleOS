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
 * @file vfs_auto.c
 * @brief Automatic VFS backend initialization
 */

#include "vfs_config.h"
#include "vfs_core.h"
#include "esp_log.h"
#include <inttypes.h>

static const char *TAG = "vfs_auto";

static bool s_backend_mounted = false;

/* ============================================================================
 * BACKEND DECLARATIONS
 * ============================================================================ */

#ifdef VFS_USE_SD_CARD
extern esp_err_t vfs_register_sd_backend(void);
extern esp_err_t vfs_unregister_sd_backend(void);
extern bool vfs_sdcard_is_mounted(void);
#endif

#ifdef VFS_USE_SPIFFS
extern esp_err_t vfs_register_spiffs_backend(void);
extern esp_err_t vfs_unregister_spiffs_backend(void);
#endif

#ifdef VFS_USE_LITTLEFS
extern esp_err_t vfs_register_littlefs_backend(void);
extern esp_err_t vfs_unregister_littlefs_backend(void);
extern bool vfs_littlefs_is_mounted(void);
#endif

#ifdef VFS_USE_RAMFS
extern esp_err_t vfs_register_ramfs_backend(void);
extern esp_err_t vfs_unregister_ramfs_backend(void);
#endif

/* ============================================================================
 * UNIFIED API
 * ============================================================================ */

esp_err_t vfs_init_auto(void)
{
    ESP_LOGI(TAG, "Initializing backend: %s", VFS_BACKEND_NAME);
    ESP_LOGI(TAG, "Mount point: %s", VFS_MOUNT_POINT);
    
    esp_err_t ret = ESP_FAIL;
    
    #ifdef VFS_USE_SD_CARD
        ret = vfs_register_sd_backend();
        if (ret == ESP_OK) {
            s_backend_mounted = true;
        }
    #endif
    
    #ifdef VFS_USE_SPIFFS
        ret = vfs_register_spiffs_backend();
        if (ret == ESP_OK) {
            s_backend_mounted = true;
        }
    #endif
    
    #ifdef VFS_USE_LITTLEFS
        ret = vfs_register_littlefs_backend();
        if (ret == ESP_OK) {
            s_backend_mounted = true;
        }
    #endif
    
    #ifdef VFS_USE_RAMFS
        ret = vfs_register_ramfs_backend();
        if (ret == ESP_OK) {
            s_backend_mounted = true;
        }
    #endif
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Backend %s initialized successfully", VFS_BACKEND_NAME);
    } else {
        ESP_LOGE(TAG, "Failed to initialize backend %s", VFS_BACKEND_NAME);
        s_backend_mounted = false;
    }
    
    return ret;
}

esp_err_t vfs_deinit_auto(void)
{
    ESP_LOGI(TAG, "Deinitializing backend: %s", VFS_BACKEND_NAME);
    
    esp_err_t ret = ESP_FAIL;
    
    #ifdef VFS_USE_SD_CARD
        ret = vfs_unregister_sd_backend();
    #endif
    
    #ifdef VFS_USE_SPIFFS
        ret = vfs_unregister_spiffs_backend();
    #endif
    
    #ifdef VFS_USE_LITTLEFS
        ret = vfs_unregister_littlefs_backend();
    #endif
    
    #ifdef VFS_USE_RAMFS
        ret = vfs_unregister_ramfs_backend();
    #endif
    
    if (ret == ESP_OK) {
        s_backend_mounted = false;
    }
    
    return ret;
}

const char* vfs_get_mount_point(void)
{
    return VFS_MOUNT_POINT;
}

const char* vfs_get_backend_name(void)
{
    return VFS_BACKEND_NAME;
}

bool vfs_is_mounted_auto(void)
{
    #ifdef VFS_USE_SD_CARD
        return s_backend_mounted && vfs_sdcard_is_mounted();
    #endif
    
    #ifdef VFS_USE_SPIFFS
        return s_backend_mounted;
    #endif
    
    #ifdef VFS_USE_LITTLEFS
        return s_backend_mounted && vfs_littlefs_is_mounted();
    #endif
    
    #ifdef VFS_USE_RAMFS
        return s_backend_mounted;
    #endif
    
    return false;
}

void vfs_print_info(void)
{
    ESP_LOGI(TAG, "========== VFS Info ==========");
    ESP_LOGI(TAG, "Backend: %s", VFS_BACKEND_NAME);
    ESP_LOGI(TAG, "Mount Point: %s", VFS_MOUNT_POINT);
    ESP_LOGI(TAG, "Max Files: %d", VFS_MAX_FILES);
    ESP_LOGI(TAG, "Status: %s", vfs_is_mounted_auto() ? "Mounted" : "Unmounted");
    
    if (vfs_is_mounted_auto()) {
        vfs_statvfs_t stat;
        if (vfs_statvfs(VFS_MOUNT_POINT, &stat) == ESP_OK) {
            ESP_LOGI(TAG, "Total: %" PRIu64 " KB", stat.total_bytes / 1024);
            ESP_LOGI(TAG, "Free: %" PRIu64 " KB", stat.free_bytes / 1024);
            ESP_LOGI(TAG, "Used: %" PRIu64 " KB", stat.used_bytes / 1024);
            
            float usage = stat.total_bytes > 0 ?
                ((float)stat.used_bytes / stat.total_bytes) * 100.0f : 0.0f;
            ESP_LOGI(TAG, "Usage: %.1f%%", usage);
        }
    }
    
    ESP_LOGI(TAG, "==============================");
}