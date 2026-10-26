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

"""Test strict generation and the real patched FatFS on a RAM-only block device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

from prepare import PATCH_DIR, prepare

NOTICE = (PATCH_DIR / "patch_manifest.json.license").read_text()
C_NOTICE = "\n".join("//" + (" " + line if line else "") for line in NOTICE.splitlines()) + "\n"


def must_reject(call):
    try:
        call()
    except (ValueError, OSError, subprocess.CalledProcessError):
        return
    raise AssertionError("Invalid source was accepted")


def test_generation(idf, work):
    manifest = json.loads((PATCH_DIR / "patch_manifest.json").read_text())
    source = work / "sdk"
    for name in manifest["files"]:
        dest = source / "components/fatfs" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(idf / "components/fatfs" / name, dest)
    (source / "tools/cmake").mkdir(parents=True)
    shutil.copy2(idf / "tools/cmake/version.cmake", source / "tools/cmake/version.cmake")
    out = work / "build/components/Service/fatfs"
    prepare(source, out)
    before = {p: p.stat().st_mtime_ns for p in out.rglob("*") if p.is_file()}
    prepare(source, out)
    assert all(p.stat().st_mtime_ns == stamp for p, stamp in before.items())
    original = source / "components/fatfs/src/ffconf.h"
    pristine = original.read_bytes()
    original.write_bytes(pristine + b"\n")
    must_reject(lambda: prepare(source, out))
    assert all(p.stat().st_mtime_ns == stamp for p, stamp in before.items())
    original.write_bytes(pristine)
    # Build outputs are disposable and are restored from the verified SDK source.
    (out / "src/ffconf.h").write_text("corrupted output\n")
    prepare(source, out)
    assert hashlib.sha256((out / "src/ffconf.h").read_bytes()).hexdigest() == manifest["patched_files"]["src/ffconf.h"]
    other = work / "other/components/Service/fatfs"
    prepare(source, other)
    assert (out / "src/ff.c").read_bytes() == (other / "src/ff.c").read_bytes()
    patch = work / "patch"
    shutil.copytree(PATCH_DIR, patch)
    (patch / "exfat.patch").write_text("invalid patch\n")
    must_reject(lambda: prepare(source, out, patch))
    manifest["patch_sha256"] = hashlib.sha256((patch / "exfat.patch").read_bytes()).hexdigest()
    (patch / "patch_manifest.json").write_text(json.dumps(manifest))
    fresh = work / "invalid/components/Service/fatfs"
    must_reject(lambda: prepare(source, fresh, patch))
    assert not fresh.exists()
    original.unlink()
    must_reject(lambda: prepare(source, out))
    original.write_bytes(pristine)
    (source / "tools/cmake/version.cmake").write_text("unsupported version")
    must_reject(lambda: prepare(source, out))
    assert original.read_bytes() == pristine
    return out


def test_filesystem(component, work):
    headers = work / "headers"
    (headers / "freertos").mkdir(parents=True)
    for name in ("FreeRTOS.h", "semphr.h"):
        (headers / "freertos" / name).write_text(C_NOTICE + "#pragma once\n")
    (headers / "sdkconfig.h").write_text(C_NOTICE + r"""
#define CONFIG_FATFS_USE_FASTSEEK 0
#define CONFIG_FATFS_CODEPAGE 437
#define CONFIG_FATFS_LFN_HEAP 1
#define CONFIG_FATFS_MAX_LFN 255
#define CONFIG_FATFS_API_ENCODING_UTF_8 1
#define CONFIG_FATFS_VOLUME_COUNT 2
#define CONFIG_WL_SECTOR_SIZE 512
#define CONFIG_FATFS_PER_FILE_CACHE 1
#define CONFIG_FATFS_FS_LOCK 0
#define CONFIG_FATFS_TIMEOUT_MS 1000
#define CONFIG_FATFS_USE_DYN_BUFFERS 0
#define portTICK_PERIOD_MS 1
""")
    harness = work / "filesystem.c"
    harness.write_text(C_NOTICE + r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
#define SECTOR_BYTES 512U
#define SECTOR_COUNT 131072U
#define TEST_PATH "0:/Long directory name/arquivo-ação.txt"
PARTITION VolToPart[FF_VOLUMES] = {{0, 0}, {1, 0}};
static BYTE *s_disk;
DSTATUS disk_initialize(BYTE drive) { return drive == 0 ? 0 : STA_NOINIT; }
DSTATUS disk_status(BYTE drive) { return disk_initialize(drive); }
DRESULT disk_read(BYTE drive, BYTE *data, LBA_t sector, UINT count) {
  if (drive || sector >= SECTOR_COUNT || count > SECTOR_COUNT-sector) return RES_PARERR;
  memcpy(data,s_disk+(size_t)sector*SECTOR_BYTES,(size_t)count*SECTOR_BYTES);return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *data, LBA_t sector, UINT count) {
  if (drive || sector >= SECTOR_COUNT || count > SECTOR_COUNT-sector) return RES_PARERR;
  memcpy(s_disk+(size_t)sector*SECTOR_BYTES,data,(size_t)count*SECTOR_BYTES);return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *data) {
  if (drive) return RES_PARERR;
  switch(command) {
    case CTRL_SYNC: case CTRL_TRIM:return RES_OK;
    case GET_SECTOR_COUNT:*(LBA_t *)data=SECTOR_COUNT;return RES_OK;
    case GET_SECTOR_SIZE:*(WORD *)data=SECTOR_BYTES;return RES_OK;
    case GET_BLOCK_SIZE:*(DWORD *)data=1;return RES_OK;
    default:return RES_PARERR;
  }
}
void *ff_memalloc(size_t size) {return malloc(size);}
void ff_memfree(void *ptr) {free(ptr);}
int ff_mutex_create(int volume) {(void)volume;return 1;}
void ff_mutex_delete(int volume) {(void)volume;}
int ff_mutex_take(int volume) {(void)volume;return 1;}
void ff_mutex_give(int volume) {(void)volume;}
DWORD get_fattime(void) {return 0;}
int main(void) {
  assert(FF_FS_EXFAT == 1 && FF_USE_LABEL == 0 && sizeof(FSIZE_t) == 8);
  s_disk=calloc(SECTOR_COUNT,SECTOR_BYTES);assert(s_disk);
  const BYTE formats[]={FM_EXFAT,FM_FAT32,FM_ANY};
  for(size_t i=0;i<sizeof(formats);i++) {
    memset(s_disk,0,(size_t)SECTOR_COUNT*SECTOR_BYTES);
    BYTE scratch[4096];MKFS_PARM options={formats[i],1,0,0,SECTOR_BYTES};
    assert(f_mkfs("0:",&options,scratch,sizeof(scratch))==FR_OK);
    FATFS fs;assert(f_mount(&fs,"0:",1)==FR_OK);
    assert(formats[i]==FM_EXFAT ? fs.fs_type==FS_EXFAT : fs.fs_type!=FS_EXFAT);
    assert(f_mkdir("0:/Long directory name")==FR_OK);
    FIL file;const char payload[]="TentacleOS exFAT round trip";UINT length;
    assert(f_open(&file,TEST_PATH,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    assert(f_write(&file,payload,sizeof(payload),&length)==FR_OK && length==sizeof(payload));
    assert(f_close(&file)==FR_OK);assert(f_mount(NULL,"0:",0)==FR_OK);
    assert(f_mount(&fs,"0:",1)==FR_OK);
    assert(f_open(&file,TEST_PATH,FA_READ)==FR_OK);
    char result[sizeof(payload)];assert(f_read(&file,result,sizeof(result),&length)==FR_OK);
    assert(length==sizeof(payload) && memcmp(result,payload,sizeof(payload))==0);
    assert(f_close(&file)==FR_OK);
    assert(f_rename(TEST_PATH,"0:/Long directory name/renamed.txt")==FR_OK);
    assert(f_unlink("0:/Long directory name/renamed.txt")==FR_OK);
    assert(f_unlink("0:/Long directory name")==FR_OK);
    assert(f_mount(NULL,"0:",0)==FR_OK);
  }
  free(s_disk);return 0;
}
""")
    exe = work / "filesystem"
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-O1",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-I", str(headers), "-I", str(component / "src"), str(harness),
                    str(component / "src/ff.c"), str(component / "src/ffunicode.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=120)


def test_format_lifecycle(component, work):
    source = (component / "vfs/vfs_fat_sdmmc.c").read_text()
    def function(signature):
        start = source.index(signature)
        end = source.index("{", start) + 1
        depth = 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        return source[start:end]
    harness = work / "format_lifecycle.c"
    text = C_NOTICE + r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
typedef struct {struct {size_t sector_size;} csd;} sdmmc_card_t;
typedef struct {size_t allocation_unit_size; bool use_one_fat;} esp_vfs_fat_mount_config_t;
typedef struct {const char *base_path; FATFS *fs; esp_vfs_fat_mount_config_t mount_config;} context_t;
static FATFS s_fs;
static context_t s_context;
static context_t *s_ctx[FF_VOLUMES];
static sdmmc_card_t s_card;
static bool s_no_memory, s_fail_unmount, s_fail_format, s_fail_remount;
static unsigned s_allocs, s_unmounts, s_formats, s_remounts;
static BYTE s_kind;
static bool s_get_context_id_by_card(const sdmmc_card_t *card, uint32_t *id) {
  if (card != &s_card) return false; *id=0;return true;
}
static BYTE ff_diskio_get_pdrv_card(sdmmc_card_t *card) {return card==&s_card ? 0 : 0xff;}
static size_t esp_vfs_fat_get_allocation_unit_size(size_t sector, size_t unit) {
  return unit ? unit : sector;
}
void *ff_memalloc(size_t size) {s_allocs++;return s_no_memory ? NULL : malloc(size);}
FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt) {
  (void)path;(void)opt;
  if (!fs) {s_unmounts++;if(s_fail_unmount)return FR_DISK_ERR;s_fs.fs_type=0;return FR_OK;}
  s_remounts++;if(s_fail_remount)return FR_DISK_ERR;fs->fs_type=FS_EXFAT;return FR_OK;
}
FRESULT f_mkfs(const TCHAR *path,const MKFS_PARM *options,void *work,UINT size) {
  (void)path;assert(work && size>0);s_formats++;s_kind=options->fmt;
  return s_fail_format ? FR_DISK_ERR : FR_OK;
}
static void reset(void) {
  s_context=(context_t){"/sdcard",&s_fs,{512,false}};s_ctx[0]=&s_context;s_fs.fs_type=FS_EXFAT;
  s_card.csd.sector_size=512;
  s_no_memory=s_fail_unmount=s_fail_format=s_fail_remount=false;
  s_allocs=s_unmounts=s_formats=s_remounts=0;
}
"""
    for signature in ("bool esp_vfs_fat_sdcard_is_mounted(", "static esp_err_t format_card_fm(",
                      "esp_err_t esp_vfs_fat_sdcard_format_cfg(", "esp_err_t esp_vfs_fat_sdcard_format_exfat("):
        text += function(signature) + "\n"
    text += r"""
int main(void) {
  reset();s_no_memory=true;
  assert(esp_vfs_fat_sdcard_format_exfat("/sdcard",&s_card)==ESP_ERR_NO_MEM);
  assert(!s_unmounts && !s_formats && !s_remounts && esp_vfs_fat_sdcard_is_mounted("/sdcard",&s_card));
  reset();s_fail_unmount=true;
  assert(esp_vfs_fat_sdcard_format_exfat("/sdcard",&s_card)==ESP_FAIL);
  assert(s_unmounts==1 && !s_formats && !s_remounts);
  reset();s_fail_format=true;
  assert(esp_vfs_fat_sdcard_format_exfat("/sdcard",&s_card)==ESP_FAIL);
  assert(s_formats==1 && s_remounts==1 && esp_vfs_fat_sdcard_is_mounted("/sdcard",&s_card));
  reset();s_fail_remount=true;
  assert(esp_vfs_fat_sdcard_format_exfat("/sdcard",&s_card)==ESP_FAIL);
  assert(s_formats==1 && s_remounts==1 && !esp_vfs_fat_sdcard_is_mounted("/sdcard",&s_card));
  assert(s_ctx[0]==&s_context && s_context.fs==&s_fs);
  reset();
  assert(esp_vfs_fat_sdcard_format_exfat("/wrong",&s_card)==ESP_ERR_INVALID_STATE && !s_allocs);
  assert(esp_vfs_fat_sdcard_format_exfat(NULL,&s_card)==ESP_ERR_INVALID_STATE && !s_allocs);
  assert(esp_vfs_fat_sdcard_format_exfat("/sdcard",NULL)==ESP_ERR_INVALID_STATE && !s_allocs);
  assert(!esp_vfs_fat_sdcard_is_mounted("/wrong",&s_card));
  assert(esp_vfs_fat_sdcard_format_exfat("/sdcard",&s_card)==ESP_OK && s_kind==FM_EXFAT);
  assert(s_formats==1 && s_remounts==1);
  reset();esp_vfs_fat_mount_config_t cfg={4096,true};
  assert(esp_vfs_fat_sdcard_format_cfg("/sdcard",&s_card,&cfg)==ESP_OK && s_kind==FM_ANY);
  assert(s_context.mount_config.allocation_unit_size==4096 && s_context.mount_config.use_one_fat);
  return 0;
}
"""
    harness.write_text(text)
    exe = work / "format_lifecycle"
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-O1",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-I", str(work / "headers"), "-I", str(component / "src"),
                    str(harness), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="fatfs-test-") as tmp:
        work = Path(tmp)
        component = test_generation(args.idf, work)
        test_filesystem(component, work)
        test_format_lifecycle(component, work)
    print("PASS: pinned patch generation, tamper rejection, idempotence, isolated outputs; real exFAT/FAT32/FAT RAM-disk round trips")


if __name__ == "__main__":
    main()
