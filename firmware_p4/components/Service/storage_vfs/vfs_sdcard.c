// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "vfs_sdcard.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "diskio_sdmmc.h"

#include "pin_def.h"
#include "vfs_config.h"
#include "vfs_core.h"

#ifdef VFS_USE_SD_CARD

static const char *TAG = "VFS_SDCARD";

static struct {
  bool mounted;
  sdmmc_card_t *card;
} s_sdcard = {0};

static vfs_fd_t sdcard_open(const char *path, int flags, int mode) {
  int fd = open(path, flags, mode);
  if (fd < 0) {
    ESP_LOGE(TAG, "open failed: %s", path);
  }
  return fd;
}

static ssize_t sdcard_read(vfs_fd_t fd, void *buf, size_t size) {
  return read(fd, buf, size);
}

static ssize_t sdcard_write(vfs_fd_t fd, const void *buf, size_t size) {
  return write(fd, buf, size);
}

static off_t sdcard_lseek(vfs_fd_t fd, off_t offset, int whence) {
  return lseek(fd, offset, whence);
}

static esp_err_t sdcard_close(vfs_fd_t fd) {
  return (close(fd) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_fsync(vfs_fd_t fd) {
  return (fsync(fd) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_stat(const char *path, vfs_stat_t *st) {
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

static esp_err_t sdcard_fstat(vfs_fd_t fd, vfs_stat_t *st) {
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

static esp_err_t sdcard_rename(const char *old_path, const char *new_path) {
  return (rename(old_path, new_path) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_unlink(const char *path) {
  return (unlink(path) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_truncate(const char *path, off_t length) {
  return (truncate(path, length) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_mkdir(const char *path, int mode) {
  return (mkdir(path, mode) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_rmdir(const char *path) {
  return (rmdir(path) == 0) ? ESP_OK : ESP_FAIL;
}

static vfs_dir_t sdcard_opendir(const char *path) {
  return (vfs_dir_t)opendir(path);
}

static esp_err_t sdcard_readdir(vfs_dir_t dir, vfs_stat_t *entry) {
  DIR *d = (DIR *)dir;
  struct dirent *ent = readdir(d);

  if (ent == NULL) {
    return ESP_ERR_NOT_FOUND;
  }

  strncpy(entry->name, ent->d_name, VFS_MAX_NAME - 1);
  entry->name[VFS_MAX_NAME - 1] = '\0';
  entry->type = (ent->d_type == DT_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
  entry->size = 0;

  return ESP_OK;
}

static esp_err_t sdcard_closedir(vfs_dir_t dir) {
  DIR *d = (DIR *)dir;
  return (closedir(d) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_get_fs(FATFS **fs, DWORD *free_clusters) {
  if (!s_sdcard.mounted || !s_sdcard.card) {
    return ESP_ERR_INVALID_STATE;
  }
  BYTE pdrv = ff_diskio_get_pdrv_card(s_sdcard.card);
  if (pdrv >= FF_VOLUMES || pdrv > 9) {
    return ESP_ERR_INVALID_STATE;
  }
  char drv[3] = {(char)('0' + pdrv), ':', 0};
  return f_getfree(drv, free_clusters, fs) == FR_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t sdcard_statvfs(vfs_statvfs_t *stat) {
  if (stat == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  memset(stat, 0, sizeof(*stat));
  FATFS *fs = NULL;
  DWORD free_clusters = 0;
  esp_err_t ret = sdcard_get_fs(&fs, &free_clusters);
  if (ret != ESP_OK) {
    return ret;
  }
  if (fs == NULL || fs->n_fatent < 2 || fs->csize == 0 || free_clusters > fs->n_fatent - 2) {
    return ESP_FAIL;
  }
  uint64_t total_sectors = (uint64_t)(fs->n_fatent - 2) * fs->csize;
  uint64_t free_sectors = (uint64_t)free_clusters * fs->csize;
  // Current FatFs uses 32-bit LBA; reject impossible/corrupt geometry.
  if (total_sectors > UINT32_MAX || s_sdcard.card->csd.sector_size == 0) {
    return ESP_FAIL;
  }
  stat->block_size = s_sdcard.card->csd.sector_size;
  stat->total_blocks = (uint32_t)total_sectors;
  stat->free_blocks = (uint32_t)free_sectors;
  stat->total_bytes = total_sectors * stat->block_size;
  stat->free_bytes = free_sectors * stat->block_size;
  stat->used_bytes = stat->total_bytes - stat->free_bytes;

  return ESP_OK;
}

static bool sdcard_is_mounted(void) {
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

esp_err_t vfs_sdcard_init(void) {
  if (s_sdcard.mounted) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing SD card (SDMMC/SDIO 4-bit mode)");

  esp_err_t ret;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 4;
  slot_config.clk = GPIO_SDMMC_CLK_PIN;
  slot_config.cmd = GPIO_SDMMC_CMD_PIN;
  slot_config.d0 = GPIO_SDMMC_D0_PIN;
  slot_config.d1 = GPIO_SDMMC_D1_PIN;
  slot_config.d2 = GPIO_SDMMC_D2_PIN;
  slot_config.d3 = GPIO_SDMMC_D3_PIN;
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = VFS_FORMAT_ON_FAIL,
      .max_files = VFS_MAX_FILES,
      .allocation_unit_size = 16 * 1024,
  };

  ret =
      esp_vfs_fat_sdmmc_mount(VFS_MOUNT_POINT, &host, &slot_config, &mount_config, &s_sdcard.card);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
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

  ESP_LOGI(TAG,
           "SD mounted (SDMMC): %s, %llu MB",
           s_sdcard.card->cid.name,
           ((uint64_t)s_sdcard.card->csd.capacity) * s_sdcard.card->csd.sector_size /
               (1024 * 1024));

  return ESP_OK;
}

esp_err_t vfs_sdcard_deinit(void) {
  if (!s_sdcard.mounted) {
    return ESP_ERR_INVALID_STATE;
  }

  vfs_unregister_backend(VFS_MOUNT_POINT);

  esp_err_t ret = esp_vfs_fat_sdcard_unmount(VFS_MOUNT_POINT, s_sdcard.card);

  // IDF releases the handle before unregistering the VFS path. Even if that
  // final unregister reports an error, this pointer must never be reused.
  s_sdcard.mounted = false;
  s_sdcard.card = NULL;

  return ret;
}

bool vfs_sdcard_is_mounted(void) {
  return s_sdcard.mounted;
}

bool vfs_sdcard_get_name(char *out, size_t n) {
  if (out == NULL || n == 0 || !s_sdcard.mounted || s_sdcard.card == NULL) {
    return false;
  }
  strncpy(out, s_sdcard.card->cid.name, n - 1);
  out[n - 1] = '\0';
  return true;
}

esp_err_t vfs_register_sd_backend(void) {
  return vfs_sdcard_init();
}

esp_err_t vfs_unregister_sd_backend(void) {
  return vfs_sdcard_deinit();
}

void vfs_sdcard_print_info(void) {
  if (!s_sdcard.mounted) {
    return;
  }

  ESP_LOGI(TAG, "SD card: %s", s_sdcard.card->cid.name);
  ESP_LOGI(TAG, "Type: %s", s_sdcard.card->ocr & SD_OCR_SDHC_CAP ? "SDHC/SDXC" : "SDSC");
  ESP_LOGI(TAG,
           "Capacity: %llu MB",
           ((uint64_t)s_sdcard.card->csd.capacity) * s_sdcard.card->csd.sector_size /
               (1024 * 1024));

  vfs_statvfs_t stat;
  if (vfs_statvfs(VFS_MOUNT_POINT, &stat) == ESP_OK) {
    float percent =
        stat.total_bytes > 0 ? ((float)stat.used_bytes / stat.total_bytes) * 100.0f : 0.0f;
    ESP_LOGI(TAG,
             "Used: %llu / %llu MB (%.1f%%)",
             stat.used_bytes / (1024 * 1024),
             stat.total_bytes / (1024 * 1024),
             percent);
  }
}

static esp_err_t sdcard_format(bool exfat) {
  if (!s_sdcard.mounted || s_sdcard.card == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = exfat ? esp_vfs_fat_sdcard_format_exfat(VFS_MOUNT_POINT, s_sdcard.card)
                        : esp_vfs_fat_sdcard_format(VFS_MOUNT_POINT, s_sdcard.card);
  if (!esp_vfs_fat_sdcard_is_mounted(VFS_MOUNT_POINT, s_sdcard.card)) {
    // The formatter retained the handle, but its remount failed. Release it
    // exactly once and leave the backend unmounted for a deliberate retry.
    vfs_sdcard_deinit();
    return ret == ESP_OK ? ESP_FAIL : ret;
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "SD formatted (%s)", vfs_sdcard_fs_type());
  return ESP_OK;
}

esp_err_t vfs_sdcard_format(void) {
  return sdcard_format(false);
}

esp_err_t vfs_sdcard_format_exfat(void) {
  return sdcard_format(true);
}

const char *vfs_sdcard_fs_type(void) {
  if (!s_sdcard.mounted) {
    return "--";
  }
  FATFS *fs = NULL;
  DWORD free_clusters;
  if (sdcard_get_fs(&fs, &free_clusters) != ESP_OK || fs == NULL) {
    return "?";
  }
  switch (fs->fs_type) {
    case FS_FAT12:
      return "FAT12";
    case FS_FAT16:
      return "FAT16";
    case FS_FAT32:
      return "FAT32";
    case FS_EXFAT:
      return "exFAT";
    default:
      return "?";
  }
}

static uint8_t benchmark_byte(size_t offset) {
  return (uint8_t)((offset * 33U) ^ (offset >> 8) ^ (offset >> 16) ^ 0xA5U);
}

esp_err_t vfs_sdcard_benchmark(float *out_read_mbs, float *out_write_mbs) {
  if (out_read_mbs)
    *out_read_mbs = 0.0f;
  if (out_write_mbs)
    *out_write_mbs = 0.0f;
  if (!s_sdcard.mounted) {
    return ESP_ERR_INVALID_STATE;
  }

  const size_t chunk = 64 * 1024;
  const size_t total = 1024 * 1024;
  const char *path = VFS_MOUNT_POINT "/.hb_bench.tmp";

  uint8_t *buf = heap_caps_malloc(chunk, MALLOC_CAP_SPIRAM);
  if (buf == NULL) {
    buf = malloc(chunk);
  }
  if (buf == NULL) {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t ret = ESP_FAIL;
  // Never truncate or remove a preexisting file (including a failed old test).
  int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd < 0) {
    free(buf);
    return ESP_FAIL;
  }
  FILE *f = fdopen(fd, "w+b");
  if (f == NULL) {
    close(fd);
    unlink(path); // only the file this invocation exclusively created
    free(buf);
    return ESP_FAIL;
  }

  int64_t t0 = esp_timer_get_time();
  size_t done = 0;
  bool ok = true;
  while (done < total) {
    for (size_t i = 0; i < chunk; ++i)
      buf[i] = benchmark_byte(done + i);
    if (fwrite(buf, 1, chunk, f) != chunk) {
      ok = false;
      break;
    }
    done += chunk;
  }
  if (fflush(f) != 0)
    ok = false;
  if (fsync(fileno(f)) != 0)
    ok = false;
  int64_t t1 = esp_timer_get_time();

  size_t rd = 0;
  int64_t t2 = 0, t3 = 0;
  if (ok && fseek(f, 0, SEEK_SET) != 0)
    ok = false;
  if (ok) {
    t2 = esp_timer_get_time();
    while (rd < total) {
      if (fread(buf, 1, chunk, f) != chunk) {
        ok = false;
        break;
      }
      for (size_t i = 0; i < chunk; ++i) {
        if (buf[i] != benchmark_byte(rd + i)) {
          ok = false;
          break;
        }
      }
      if (!ok)
        break;
      rd += chunk;
    }
    // Verify both exact payload length and a clean end of file.
    if (rd != total || fgetc(f) != EOF || ferror(f))
      ok = false;
    t3 = esp_timer_get_time();
  }

  if (fclose(f) != 0)
    ok = false;
  if (unlink(path) != 0)
    ok = false;
  free(buf);

  if (ok && done == total && rd == total) {
    double wsec = (double)(t1 - t0) / 1e6;
    double rsec = (double)(t3 - t2) / 1e6;
    if (out_write_mbs)
      *out_write_mbs = wsec > 0 ? (float)((double)done / 1e6 / wsec) : 0.0f;
    if (out_read_mbs)
      *out_read_mbs = rsec > 0 ? (float)((double)rd / 1e6 / rsec) : 0.0f;
    ret = ESP_OK;
  }
  return ret;
}

esp_err_t vfs_sdcard_detach_for_msc(void **out_card) {
  if (out_card == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  *out_card = NULL;

  if (s_sdcard.mounted) {
    esp_err_t r = vfs_sdcard_deinit();
    if (r != ESP_OK) {
      ESP_LOGE(TAG, "detach: unmount failed: %s", esp_err_to_name(r));
      return r;
    }
  }

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 4;
  slot_config.clk = GPIO_SDMMC_CLK_PIN;
  slot_config.cmd = GPIO_SDMMC_CMD_PIN;
  slot_config.d0 = GPIO_SDMMC_D0_PIN;
  slot_config.d1 = GPIO_SDMMC_D1_PIN;
  slot_config.d2 = GPIO_SDMMC_D2_PIN;
  slot_config.d3 = GPIO_SDMMC_D3_PIN;
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_err_t r = sdmmc_host_init();
  if (r != ESP_OK) {
    ESP_LOGE(TAG, "detach: host init: %s", esp_err_to_name(r));
    return r;
  }
  r = sdmmc_host_init_slot(host.slot, &slot_config);
  if (r != ESP_OK) {
    ESP_LOGE(TAG, "detach: slot init: %s", esp_err_to_name(r));
    sdmmc_host_deinit();
    return r;
  }
  sdmmc_card_t *card = calloc(1, sizeof(sdmmc_card_t));
  if (card == NULL) {
    sdmmc_host_deinit();
    return ESP_ERR_NO_MEM;
  }
  r = sdmmc_card_init(&host, card);
  if (r != ESP_OK) {
    ESP_LOGE(TAG, "detach: card init: %s", esp_err_to_name(r));
    free(card);
    sdmmc_host_deinit();
    return r;
  }

  *out_card = card;
  ESP_LOGI(TAG, "SD detached from app FAT and handed to USB MSC (raw)");
  return ESP_OK;
}

esp_err_t vfs_sdcard_reattach_after_msc(void *card) {
  (void)sdmmc_host_deinit();
  if (card != NULL) {
    free(card);
  }
  return vfs_sdcard_init();
}

#endif
