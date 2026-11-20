#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Run production SD lifecycle/health code against a reentrant format race."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
sd = (ROOT / 'firmware_p4/components/Service/storage_vfs/vfs_sdcard.c').read_text()
health = (ROOT / 'firmware_p4/components/Service/storage_api/storage_init.c').read_text()
code = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef unsigned char BYTE;
typedef int FRESULT;
typedef unsigned UINT;
typedef bool (*vfs_sdcard_format_cb_t)(const char *, uint8_t, void *);
static vfs_sdcard_format_cb_t s_format_cb;
static void *s_format_ctx;
static BYTE s_format_drive;
static bool s_format_cancelled;
#define FR_OK 0
#define SD_VOLUME_LABEL "Highboy"
#define SD_INVALID_DRIVE UINT8_MAX
#define SD_LABEL_PATH_SIZE (sizeof(SD_VOLUME_LABEL) + 3)
static BYTE ff_diskio_get_pdrv_card(void *card) { (void)card; return 1; }
static int label_result;
static FRESULT f_setlabel(const char *label) { assert(!strcmp(label,"1:Highboy")); return label_result; }
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NOT_FINISHED 4
#define VFS_USE_SD_CARD 1
#define VFS_MOUNT_POINT "/sdcard"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static atomic_flag s_access = ATOMIC_FLAG_INIT;
static struct { bool mounted; void *card; } s_sdcard;
static bool fs_mounted, busy;
static int format_result, deinitializations, probes, formats;
bool vfs_sdcard_try_acquire(void);
void vfs_sdcard_release(void);
int vfs_sdcard_deinit(void);
int storage_check_health(void);
int vfs_sdcard_format_exfat(void);
int vfs_sdcard_detach_for_msc(void **);
static int check_health(void) { probes++; return ESP_OK; }
static int esp_vfs_fat_suspend(const char *path) {
 (void)path; if(busy) return ESP_ERR_INVALID_STATE; fs_mounted=false; return ESP_OK;
}
static int esp_vfs_fat_resume(const char *path) { (void)path; fs_mounted=true; return ESP_OK; }
static bool esp_vfs_fat_sdcard_is_mounted(const char *path, void *card) {
 (void)path; (void)card; return fs_mounted;
}
static int deinit_card(void) {
 deinitializations++; s_sdcard.card=NULL; s_sdcard.mounted=false; return ESP_OK;
}
static int detach_card(void **card) { (void)card; assert(0); return ESP_FAIL; }
static int esp_vfs_fat_sdcard_format_exfat(const char *path, void *card) {
 (void)path; formats++; assert(card && !fs_mounted);
 int before=probes; void *other=NULL;
 assert(storage_check_health()==ESP_ERR_INVALID_STATE && probes==before);
 assert(vfs_sdcard_deinit()==ESP_ERR_INVALID_STATE && !deinitializations);
 assert(vfs_sdcard_detach_for_msc(&other)==ESP_ERR_INVALID_STATE);
 assert(vfs_sdcard_format_exfat()==ESP_ERR_INVALID_STATE);
 assert(s_sdcard.card==card);
 fs_mounted=format_result==ESP_OK; return format_result;
}
#define esp_vfs_fat_sdcard_format esp_vfs_fat_sdcard_format_exfat
"""
for signature in ['int ff_mkfs_progress(', 'bool vfs_sdcard_try_acquire(', 'void vfs_sdcard_release(',
                  'esp_err_t vfs_sdcard_deinit(', 'esp_err_t vfs_sdcard_detach_for_msc(',
                  'static esp_err_t sdcard_format(', 'esp_err_t vfs_sdcard_format_with_progress(', 'esp_err_t vfs_sdcard_format_exfat(']:
    code += function(sd, signature) + '\n'
code += function(health, 'esp_err_t storage_check_health(')
code += r"""
static void reset(void) {
 s_sdcard.card=&s_sdcard; s_sdcard.mounted=fs_mounted=true;
 busy=false; label_result=0; format_result=deinitializations=probes=formats=0;
 assert(vfs_sdcard_try_acquire()); vfs_sdcard_release();
}
static bool cancel_now(const char *stage, uint8_t percent, void *ctx) {
 (void)stage; (void)percent; (void)ctx; return false;
}
static bool cancel_label(const char *stage, uint8_t percent, void *ctx) {
 (void)stage; (void)ctx; return percent < 98;
}
int main(void) {
 reset(); assert(vfs_sdcard_format_with_progress(true,cancel_now,NULL)==ESP_ERR_NOT_FINISHED);
 assert(!formats && !deinitializations && fs_mounted);
 reset(); assert(vfs_sdcard_format_with_progress(true,cancel_label,NULL)==ESP_ERR_NOT_FINISHED);
 assert(formats==1 && !deinitializations && fs_mounted);
 reset(); assert(vfs_sdcard_format_exfat()==ESP_OK && formats==1 && !deinitializations);
 assert(storage_check_health()==ESP_OK && probes==1);
 reset(); label_result=ESP_FAIL;
 assert(vfs_sdcard_format_exfat()==ESP_FAIL && formats==1 && !deinitializations && fs_mounted);
 reset(); busy=true; assert(vfs_sdcard_format_exfat()==ESP_ERR_INVALID_STATE && !formats && fs_mounted);
 reset(); format_result=ESP_FAIL;
 assert(vfs_sdcard_format_exfat()==ESP_FAIL && deinitializations==1 && !s_sdcard.card);
 reset(); format_result=ESP_ERR_NO_MEM;
 assert(vfs_sdcard_format_exfat()==ESP_ERR_NO_MEM && !deinitializations && fs_mounted);
 reset(); assert(vfs_sdcard_try_acquire());
 assert(vfs_sdcard_format_exfat()==ESP_ERR_INVALID_STATE && !formats);
 vfs_sdcard_release(); assert(vfs_sdcard_format_exfat()==ESP_OK);
}
"""
with tempfile.TemporaryDirectory(prefix='hb-format-') as temporary:
    work=Path(temporary)
    (work/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
print('PASS: format excludes health/remount/USB/reformat; open files and failure cleanup')
