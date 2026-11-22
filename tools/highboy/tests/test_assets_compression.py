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

"""Validate generated frames and the production decoder with native LVGL (no device)."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
BASE = Path(sys.argv[1]).resolve()
WORK = BASE / "assets_compression"
WORK.mkdir(parents=True, exist_ok=True)
UI = ROOT / "firmware_p4/components/Applications/ui"
LVGL = ROOT / "firmware_p4/managed_components/lvgl__lvgl"
CONVERTER = ROOT / "tools/png_to_bin"
sys.path.insert(0, str(CONVERTER))
from compress_frames import compress_file, IMAGE_HEADER, IMAGE_COMPRESSED

NOTICE = (UI / "assets_manager.c").read_text().split('#include', 1)[0]


def write(name, content):
    path = WORK / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(NOTICE + content)
    return path


raw = WORK / "raw"
encoded = WORK / "encoded"
for folder in (raw, encoded):
    if folder.exists():
        shutil.rmtree(folder)
shutil.copytree(ROOT / "firmware_p4/assets/frames", raw)
subprocess.run([sys.executable, str(CONVERTER / "generate_rotated_frames.py"), str(raw)], check=True)
subprocess.run([sys.executable, str(CONVERTER / "LVGLImage.py"), str(raw), '--ofmt', 'BIN',
                '--cf', 'ARGB8888', '--align', '1', '-o', str(raw)], check=True,
               stdout=subprocess.DEVNULL)
encoded.mkdir()
files = sorted(raw.glob('*.bin'))
for path in files:
    destination = encoded / path.name
    shutil.copyfile(path, destination)
    compress_file(destination)
    if destination.read_bytes() != path.read_bytes():
        assert destination.with_suffix('.bin.license').is_file()

sample = next(p for p in encoded.glob('*.bin')
              if IMAGE_HEADER.unpack_from(p.read_bytes())[2] == IMAGE_COMPRESSED)
data = sample.read_bytes()
reference = raw / sample.name
cases = [(p.name, str(p), str(raw / p.name), True) for p in encoded.glob('*.bin')]
cases += [('raw-' + p.name, str(p), str(p), True) for p in files]
mutations = {
    'short-header': data[:8], 'short-compression-header': data[:20],
    'truncated-payload': data[:-1], 'trailing-data': data + b'x',
    'bad-magic': b'\0' + data[1:],
    'bad-method': data[:12] + struct.pack('<I', 99) + data[16:],
    'oversized-packed': data[:16] + struct.pack('<I', 0x7fffffff) + data[20:],
    'wrong-decoded-size': data[:20] + struct.pack('<I', 1) + data[24:],
    'corrupt-payload': data[:24] + b'\xff' * (len(data) - 24),
    'short-raw': reference.read_bytes()[:-1],
}
for name, payload in mutations.items():
    path = WORK / (name + '.bin')
    path.write_bytes(payload)
    path.with_suffix('.bin.license').write_text(NOTICE)
    cases.append((name, str(path), str(reference), False))
manifest = WORK / 'cases.txt'
manifest.write_text(''.join(f'{int(valid)}\t{path}\t{ref}\n' for _, path, ref, valid in cases))
manifest.with_suffix('.txt.license').write_text(NOTICE)
write('lv_conf.h', (BASE / 'lv_conf.h').read_text() + '\n#define LV_USE_LZ4 1\n#define LV_USE_LZ4_INTERNAL 1\n')
write('esp_log.h', '#define ESP_LOGE(tag, ...) ((void)(tag))\n#define ESP_LOGW(tag, ...) ((void)(tag))\n#define ESP_LOGI(tag, ...) ((void)(tag))\n')
write('esp_heap_caps.h', '#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n')
write('lvgl_glue.h', 'static bool lvgl_glue_lock(int timeout) { (void)timeout; return true; }\nstatic void lvgl_glue_unlock(void) {}\n')
write('esp_littlefs.h', r"""
typedef int esp_err_t;
#define ESP_OK 0
typedef struct {const char *base_path, *partition_label; bool format_if_mount_failed, dont_mount;} esp_vfs_littlefs_conf_t;
static bool esp_littlefs_mounted(const char *label) {(void)label; return true;}
static int esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *cfg) {(void)cfg; return 0;}
static int esp_littlefs_info(const char *label, size_t *total, size_t *used) {(void)label; *total=0; *used=0; return 0;}
""")
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "draw/lv_image_decoder_private.h"
static bool fail_caps;
static unsigned live_caps;
static void *caps[8];
static const char *sd_path;
static void *heap_caps_malloc(size_t size, unsigned cap) {
  assert(cap==3);
  if(fail_caps) return NULL;
  void *p=malloc(size); assert(p);
  for(unsigned i=0;i<8;i++) if(!caps[i]) {caps[i]=p; live_caps++; return p;}
  abort();
}
static void test_free(void *p) {
  if(p) for(unsigned i=0;i<8;i++) if(caps[i]==p) {caps[i]=NULL; live_caps--; break;}
  free(p);
}
static FILE *test_fopen(const char *path, const char *mode) {
  return fopen(strncmp(path,"/sdcard/",8)==0 ? sd_path : path,mode);
}
#define fopen test_fopen
#define free test_free
#include "assets_manager.c"
#undef free
#undef fopen
int main(int argc, char **argv) {
  assert(argc==3); lv_init(); assets_manager_init();
  lv_image_cache_resize(0,true);
  FILE *cases=fopen(argv[1],"r"); assert(cases);
  char path[1024], reference[1024]; int valid; unsigned count=0;
  while(fscanf(cases,"%d\t%1023s\t%1023s\n",&valid,path,reference)==3) {
    bin_header_t hdr; assert(read_bin_header(reference,&hdr));
    lv_image_dsc_t expected={0}; header_to_dsc(&hdr,&expected);
    size_t size=expected.data_size;
    uint8_t *pixels=malloc(size), *decoded=malloc(size); assert(pixels && decoded);
    assert(read_pixels(reference,pixels,size));
    assert(read_pixels(path,decoded,size)==(bool)valid); assert(live_caps==0);
    if(valid) {
      assert(memcmp(pixels,decoded,size)==0);
      lv_image_dsc_t *image=assets_get(path); assert(image);
      lv_image_decoder_dsc_t dsc={0};
      assert(lv_image_decoder_open(&dsc,image,NULL)==LV_RESULT_OK);
      assert(dsc.decoded && dsc.decoded->data_size==size);
      assert(memcmp(dsc.decoded->data,pixels,size)==0);
      lv_image_decoder_close(&dsc);
      sd_path=path;
      asset_node_t node={.file="/sdcard/frame.bin",.dsc=expected};
      load_sd_pixels(&node); assert(node.sd_data && live_caps==1);
      assert(memcmp(node.sd_data,pixels,size)==0);
      fail_caps=true; load_sd_pixels(&node); assert(!node.sd_data && live_caps==0);
      fail_caps=false;
    }
    free(decoded); free(pixels); count++;
  }
  fclose(cases);
  lv_image_dsc_t *image=assets_get(argv[2]); assert(image);
  fail_caps=true;
  lv_image_decoder_dsc_t dsc={0};
  assert(lv_image_decoder_open(&dsc,image,NULL)==LV_RESULT_INVALID);
  assert(live_caps==0); fail_caps=false;
  for(unsigned i=0;i<3;i++) {
    lv_image_cache_resize(1024*1024,true);
    assert(lv_image_decoder_open(&dsc,image,NULL)==LV_RESULT_OK);
    lv_image_decoder_close(&dsc); assets_manager_evict_cache();
    assert(live_caps==0);
  }
  char directory[1024]; strlcpy(directory,argv[2],sizeof(directory));
  char *slash=strrchr(directory,'/'); assert(slash); *slash='\0';
  assert(assets_load_from_sd(directory,"/virtual/frames"));
  for(int i=0;i<s_overrides[0].file_count;i++)
    assert(strstr(s_overrides[0].files[i],".license")==NULL);
  char virtual_path[1024]; snprintf(virtual_path,sizeof(virtual_path),"/virtual/frames/%s",slash+1);
  lv_image_dsc_t *overridden=assets_get(virtual_path); assert(overridden);
  assert(lv_image_decoder_open(&dsc,overridden,NULL)==LV_RESULT_OK);
  lv_image_decoder_close(&dsc); assets_manager_evict_cache();
  assets_unload_sd(); assets_manager_free_all(); lv_deinit();
  printf("PASS: %u raw/LZ4/malformed cases, LVGL decoding/cache, SD preload, allocation failure\n",count);
}
"""
test = write('test.c', code)
command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-g', '-fsanitize=address,undefined',
           '-fno-sanitize-recover=all', '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
           '-I' + str(WORK), '-I' + str(UI), '-I' + str(UI / 'include'),
           '-I' + str(LVGL), '-I' + str(LVGL / 'src'), str(test),
           str(LVGL / 'src/libs/lz4/lz4.c'), str(BASE / 'build/lvgl-build/lib/liblvgl.a'),
           '-lm', '-o', str(WORK / 'test')]
subprocess.run(command, check=True, timeout=60)
subprocess.run([str(WORK / 'test'), str(manifest), str(sample)], check=True, timeout=60)
