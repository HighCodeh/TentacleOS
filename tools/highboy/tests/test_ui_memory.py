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



from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[3]
source=ROOT/'firmware_p4/components/Service/lvgl/lvgl_memory.c'
with tempfile.TemporaryDirectory() as temp:
 d=Path(temp)
 (d/'sdkconfig.h').write_text('#define CONFIG_SPIRAM 1\n')
 (d/'lvgl.h').write_text('''#pragma once
#include <stddef.h>
#include <stdbool.h>
#define LV_STDLIB_CUSTOM 255
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM
typedef void *lv_mem_pool_t;
typedef struct {size_t total_size,free_size;} lv_mem_monitor_t;
typedef enum {LV_RESULT_OK,LV_RESULT_INVALID} lv_result_t;
''')
 (d/'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#include <stdbool.h>
#define MALLOC_CAP_SPIRAM 1024
#define MALLOC_CAP_8BIT 4
void *heap_caps_malloc(size_t,int);
void *heap_caps_realloc(void *,size_t,int);
void heap_caps_free(void *);
bool heap_caps_check_integrity(int,bool);
''')
 test=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "lvgl.h"
#include "esp_heap_caps.h"
static bool fail,integrity=true;
static unsigned calls;
void *heap_caps_malloc(size_t n,int caps){assert(caps==1028);calls++;return fail?NULL:malloc(n);}
void *heap_caps_realloc(void *p,size_t n,int caps){assert(caps==1028);calls++;return fail?NULL:realloc(p,n);}
void heap_caps_free(void *p){free(p);}
bool heap_caps_check_integrity(int caps,bool print){assert(caps==1028 && !print);return integrity;}
void *lv_malloc_core(size_t);
void *lv_realloc_core(void *,size_t);
void lv_free_core(void *);
lv_result_t lv_mem_test_core(void);
int main(void){
 for(size_t n=1;n<=65536;n*=2){
  uint8_t *p=lv_malloc_core(n);assert(p);memset(p,0x5a,n);
  uint8_t *q=lv_realloc_core(p,n+13);assert(q);
  for(size_t i=0;i<n;i++)assert(q[i]==0x5a);
  lv_free_core(q);
 }
 uint8_t *p=lv_malloc_core(16);memset(p,0xa5,16);fail=true;
 unsigned before=calls;assert(lv_malloc_core(32)==NULL && calls==before+1);
 assert(lv_realloc_core(p,64)==NULL && p[0]==0xa5);lv_free_core(p);lv_free_core(NULL);
 assert(lv_mem_test_core()==LV_RESULT_OK);integrity=false;assert(lv_mem_test_core()==LV_RESULT_INVALID);
 puts("PASS UI allocator: PSRAM for small/large blocks, no internal fallback, realloc failure and integrity reporting");
}
'''
 (d/'test.c').write_text(test)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(d),str(source),str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True,timeout=10)
