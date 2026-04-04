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
#include "vfs_config.h"
#include "pin_def.h"
#include "spi.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/sdmmc_defs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include "vfs_sdcard.h"

#ifdef VFS_USE_SD_CARD

static const char *TAG = "vfs_sdcard";

static struct {
    bool mounted;
    sdmmc_card_t *card;
    bool we_initialized_bus;
} s_sdcard = {0};

static vfs_fd_t sdcard_open(const char *path, int flags, int mode)
{
    int fd = open(path, flags, mode);
    if (fd < 0) {
        ESP_LOGE(TAG, "open failed: %s", path);
    }
    return fd;
}

static ssize_t sdcard_read(vfs_fd_t fd, void *buf, size_t size)
{
    return read(fd, buf, size);
}

static ssize_t sdcard_write(vfs_fd_t fd, const void *buf, size_t size)
{
    return write(fd, buf, size);
}

static off_t sdcard_lseek(vfs_fd_t fd, off_t offset, int whence)
{
    return lseek(fd, offset, whence);
}

static esp_err_t sdcard_close(vfs_fd_t fd)
{
    return (close(fd) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_fsync(vfs_fd_t fd)
{
    return (fsync(fd) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_stat(const char *path, vfs_stat_t *st)
{
    struct stat native_stat;
    if (stat(path, &native_stat) != 0) {
        return ESP_FAIL;
    }
    
    const char *name = strrchr(path, '/');
    strncpy(st->name, name ? name + 1 : path, VFS_MAX_NAME - 1);
    st->name[VFS_MAX_NAME - 1] = '\0';
    
    st->type = S_ISDIR(native_stat.st_mode) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    st->size = native_stat.st_size;
    st->mtime = native_stat.st_mtime;
    st->ctime = native_stat.st_ctime;
    st->is_hidden = false;
    st->is_readonly = false;
    
    return ESP_OK;
}

static esp_err_t sdcard_fstat(vfs_fd_t fd, vfs_stat_t *st)
{
    struct stat native_stat;
    if (fstat(fd, &native_stat) != 0) {
        return ESP_FAIL;
    }
    
    st->name[0] = '\0';
    st->type = S_ISDIR(native_stat.st_mode) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    st->size = native_stat.st_size;
    st->mtime = native_stat.st_mtime;
    st->ctime = native_stat.st_ctime;
    st->is_hidden = false;
    st->is_readonly = false;
    
    return ESP_OK;
}

static esp_err_t sdcard_rename(const char *old_path, const char *new_path)
{
    return (rename(old_path, new_path) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_unlink(const char *path)
{
    return (unlink(path) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_truncate(const char *path, off_t length)
{
    return (truncate(path, length) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_mkdir(const char *path, int mode)
{
    return (mkdir(path, mode) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_rmdir(const char *path)
{
    return (rmdir(path) == 0) ? ESP_OK : ESP_FAIL;
}

static vfs_dir_t sdcard_opendir(const char *path)
{
    return (vfs_dir_t)opendir(path);
}

static esp_err_t sdcard_readdir(vfs_dir_t dir, vfs_stat_t *entry)
{
    DIR *d = (DIR *)dir;
    struct dirent *ent = readdir(d);
    
    if (!ent) {
        return ESP_ERR_NOT_FOUND;
    }
    
    strncpy(entry->name, ent->d_name, VFS_MAX_NAME - 1);
    entry->name[VFS_MAX_NAME - 1] = '\0';
    entry->type = (ent->d_type == DT_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    entry->size = 0;
    
    return ESP_OK;
}

static esp_err_t sdcard_closedir(vfs_dir_t dir)
{
    DIR *d = (DIR *)dir;
    return (closedir(d) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_statvfs(vfs_statvfs_t *stat)
{
    if (!s_sdcard.mounted || !s_sdcard.card) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint64_t total = ((uint64_t)s_sdcard.card->csd.capacity) * s_sdcard.card->csd.sector_size;
    
    stat->total_bytes = total;
    stat->block_size = s_sdcard.card->csd.sector_size;
    stat->total_blocks = s_sdcard.card->csd.capacity;
    
    FATFS *fs;
    DWORD free_clusters;
    
    if (f_getfree("0:", &free_clusters, &fs) == FR_OK) {
        uint64_t free_sectors = free_clusters * fs->csize;
        stat->free_bytes = free_sectors * fs->ssize;
        stat->free_blocks = free_sectors;
    } else {
        stat->free_bytes = 0;
        stat->free_blocks = 0;
    }
    
    stat->used_bytes = stat->total_bytes - stat->free_bytes;
    
    return ESP_OK;
}

static bool sdcard_is_mounted(void)
{
    return s_sdcard.mounted;
}

static const vfs_backend_ops_t s_sdcard_ops = {
    .init = NULL,
    .deinit = NULL,
    .is_mounted = sdcard_is_mounted,
    .open = sdcard_open,
    .read = sdcard_read,
    .write = sdcard_write,
    .lseek = sdcard_lseek,
    .close = sdcard_close,
    .fsync = sdcard_fsync,
    .stat = sdcard_stat,
    .fstat = sdcard_fstat,
    .rename = sdcard_rename,
    .unlink = sdcard_unlink,
    .truncate = sdcard_truncate,
    .mkdir = sdcard_mkdir,
    .rmdir = sdcard_rmdir,
    .opendir = sdcard_opendir,
    .readdir = sdcard_readdir,
    .closedir = sdcard_closedir,
    .statvfs = sdcard_statvfs,
};

esp_err_t vfs_sdcard_init(void)
{
    if (s_sdcard.mounted) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing SD card");
    
    esp_err_t ret;
    s_sdcard.we_initialized_bus = false;
    
    ret = spi_init();
    
    if (ret == ESP_OK) {
        s_sdcard.we_initialized_bus = false;
    } else if (ret == ESP_ERR_INVALID_STATE) {
        s_sdcard.we_initialized_bus = false;
    } else {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num = SPI_MOSI_PIN,
            .miso_io_num = SPI_MISO_PIN,
            .sclk_io_num = SPI_SCLK_PIN,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000,
            .flags = SPICOMMON_BUSFLAG_MASTER,
        };
        
        ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
        
        if (ret == ESP_OK) {
            s_sdcard.we_initialized_bus = true;
        } else if (ret == ESP_ERR_INVALID_STATE) {
            s_sdcard.we_initialized_bus = false;
        } else {
            ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    vTaskDelay(pdMS_TO_TICKS(200));
    
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CARD_CS_PIN;
    slot_config.host_id = SPI2_HOST;
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = VFS_FORMAT_ON_FAIL,
        .max_files = VFS_MAX_FILES,
        .allocation_unit_size = 16 * 1024,
    };
    
    ret = esp_vfs_fat_sdspi_mount(
        VFS_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &s_sdcard.card
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
        if (s_sdcard.we_initialized_bus) {
            spi_bus_free(SPI2_HOST);
            s_sdcard.we_initialized_bus = false;
        }
        return ret;
    }
    
    s_sdcard.mounted = true;
    
    vfs_backend_config_t backend_config = {
        .type = VFS_BACKEND_SD_FAT,
        .mount_point = VFS_MOUNT_POINT,
        .ops = &s_sdcard_ops,
        .private_data = &s_sdcard,
    };
    
    ret = vfs_register_backend(&backend_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register failed");
        vfs_sdcard_deinit();
        return ret;
    }
    
    ESP_LOGI(TAG, "SD mounted: %s, %llu MB", 
             s_sdcard.card->cid.name,
             ((uint64_t)s_sdcard.card->csd.capacity) * s_sdcard.card->csd.sector_size / (1024 * 1024));
    
    return ESP_OK;
}

esp_err_t vfs_sdcard_deinit(void)
{
    if (!s_sdcard.mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    vfs_unregister_backend(VFS_MOUNT_POINT);
    
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(VFS_MOUNT_POINT, s_sdcard.card);
    
    if (ret == ESP_OK) {
        s_sdcard.mounted = false;
        s_sdcard.card = NULL;
        
        if (s_sdcard.we_initialized_bus) {
            spi_bus_free(SPI2_HOST);
            s_sdcard.we_initialized_bus = false;
        }
    }
    
    return ret;
}

bool vfs_sdcard_is_mounted(void)
{
    return s_sdcard.mounted;
}

esp_err_t vfs_register_sd_backend(void)
{
    return vfs_sdcard_init();
}

esp_err_t vfs_unregister_sd_backend(void)
{
    return vfs_sdcard_deinit();
}

void vfs_sdcard_print_info(void)
{
    if (!s_sdcard.mounted) {
        return;
    }
    
    ESP_LOGI(TAG, "SD card: %s", s_sdcard.card->cid.name);
    ESP_LOGI(TAG, "Type: %s", 
             s_sdcard.card->ocr & SD_OCR_SDHC_CAP ? "SDHC/SDXC" : "SDSC");
    ESP_LOGI(TAG, "Capacity: %llu MB", 
             ((uint64_t)s_sdcard.card->csd.capacity) * s_sdcard.card->csd.sector_size / (1024 * 1024));
    
    vfs_statvfs_t stat;
    if (vfs_get_total_space(VFS_MOUNT_POINT, &stat.total_bytes) == ESP_OK &&
        vfs_get_free_space(VFS_MOUNT_POINT, &stat.free_bytes) == ESP_OK) {
        stat.used_bytes = stat.total_bytes - stat.free_bytes;
        float percent = stat.total_bytes > 0 ? 
            ((float)stat.used_bytes / stat.total_bytes) * 100.0f : 0.0f;
        ESP_LOGI(TAG, "Used: %llu / %llu MB (%.1f%%)", 
                 stat.used_bytes / (1024 * 1024),
                 stat.total_bytes / (1024 * 1024),
                 percent);
    }
}

esp_err_t vfs_sdcard_format(void)
{
    if (!s_sdcard.mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = vfs_sdcard_deinit();
    if (ret != ESP_OK) {
        return ret;
    }
    
    return vfs_sdcard_init();
}

#endif // VFS_USE_SD_CARD
