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

"""Exercise the C5 asset loader against real host files and allocation/read faults."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/nfc_api"))
from test_emu_tx import extract
source = (ROOT / "firmware_c5/components/Service/storage_assets/storage_assets.c").read_text()
notice = source.split("#include", 1)[0]

HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
typedef int esp_err_t;
enum {ESP_OK,ESP_FAIL,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_NOT_FOUND};
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ASSETS_MOUNT_POINT "assets"
#define ESP_LOGE(...) (++errors)
#define ESP_LOGI(...) ((void)0)
static bool s_is_initialized = true;
static bool allocation_fails, overflow_size;
static int errors, read_fault, read_calls, allocation_calls;
static size_t allocation_size;
static void *heap_caps_malloc(size_t size, unsigned caps) {
  assert(size>0 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  allocation_calls++;allocation_size=size;
  void *data=allocation_fails ? NULL : malloc(size);
  if(data)memset(data,0xa5,size);
  return data;
}
/* FILE_READERS */
static esp_err_t storage_assets_get_file_size(const char *name,size_t *out_size) {
  if(overflow_size){*out_size=SIZE_MAX;return ESP_OK;}
  return get_file_size_impl(name,out_size);
}
static esp_err_t storage_assets_read_file(const char *name,uint8_t *data,size_t size,size_t *out_read) {
  read_calls++;
  if(read_fault==1)return ESP_FAIL;
  esp_err_t ret=read_file_impl(name,data,size,out_read);
  if(ret==ESP_OK && read_fault==2 && *out_read>0)--*out_read;
  return ret;
}
/* LOAD */
static void write_fixture(const char *name,const void *data,size_t size) {
  char path[128];assert(snprintf(path,sizeof(path),"assets/%s",name)>0);
  FILE *file=fopen(path,"wb");assert(file);
  assert(fwrite(data,1,size,file)==size && fclose(file)==0);
}
int main(void) {
  assert(mkdir(ASSETS_MOUNT_POINT,0700)==0);
  write_fixture("empty.json","",0);
  size_t size=99;
  uint8_t *buffer=storage_assets_load_file("empty.json",&size);
  assert(buffer && size==0 && buffer[0]==0 && errors==0 && read_calls==0);
  assert(allocation_size==1);free(buffer);
  const uint8_t binary[]={0x00,0xff,0x7f,0x80,0x31,0x00};
  write_fixture("binary.bin",binary,sizeof(binary));
  buffer=storage_assets_load_file("binary.bin",&size);
  assert(buffer && size==sizeof(binary) && !memcmp(buffer,binary,size) && buffer[size]==0);
  assert(allocation_size==size+1);free(buffer);
  const char json[]="[{\"ssid\":\"SavedNetwork\",\"password\":\"fixture-only\"}]";
  write_fixture("networks.json",json,strlen(json));
  buffer=storage_assets_load_file("networks.json",NULL);
  assert(buffer && buffer[strlen(json)]==0);
  cJSON *root=cJSON_Parse((const char*)buffer);assert(root && cJSON_IsArray(root));
  cJSON_Delete(root);free(buffer);
  const char malformed[]="{\"unterminated\":\"value";
  write_fixture("bad.json",malformed,strlen(malformed));
  buffer=storage_assets_load_file("bad.json",&size);assert(buffer);
  assert(cJSON_Parse((const char*)buffer)==NULL);free(buffer);
  for(int fault=1;fault<=2;fault++) {
    read_fault=fault;size=99;
    assert(!storage_assets_load_file("networks.json",&size) && size==0);
  }
  read_fault=0;allocation_fails=true;size=99;
  int before=read_calls;
  assert(!storage_assets_load_file("networks.json",&size) && size==0 && read_calls==before);
  allocation_fails=false;overflow_size=true;before=allocation_calls;size=99;
  assert(!storage_assets_load_file("networks.json",&size) && size==0 && allocation_calls==before);
  overflow_size=false;size=99;
  assert(!storage_assets_load_file("missing.json",&size) && size==0);
  size=99;assert(!storage_assets_load_file(NULL,&size) && size==0);
  s_is_initialized=false;size=99;
  assert(!storage_assets_load_file("networks.json",&size) && size==0);
  puts("PASS: C5 asset loader empty/binary/JSON files, terminator, PSRAM failure, short/error reads, missing/unmounted/overflow paths");
}
"""
readers = extract(source,"esp_err_t storage_assets_get_file_size(const char *filename, size_t *out_size) {").replace("storage_assets_get_file_size(","get_file_size_impl(")
readers += "\n" + extract(source,"esp_err_t\nstorage_assets_read_file(const char *filename, uint8_t *buffer, size_t size, size_t *out_read) {").replace("storage_assets_read_file(","read_file_impl(")
fixture = HARNESS.replace("/* FILE_READERS */",readers).replace("/* LOAD */",extract(source,"uint8_t *storage_assets_load_file(const char *filename, size_t *out_size) {"))
cjson = ROOT / "tools/dependencies/build/managed_components/espressif__cjson/cJSON"
with tempfile.TemporaryDirectory(prefix="highboy-c5-assets-") as temporary:
    work=Path(temporary)
    (work/"test.c").write_text(notice+fixture)
    subprocess.run([*shlex.split(os.environ.get("CC","cc")),"-std=c11","-Wall","-Wextra","-Werror",
                    "-Wno-unused-function","-Wno-deprecated-declarations",
                    "-fsanitize=address,undefined","-fno-sanitize-recover=all",
                    "-I"+str(cjson),str(work/"test.c"),str(cjson/"cJSON.c"),"-o",str(work/"test")],check=True)
    subprocess.run([str(work/"test")],cwd=work,check=True,timeout=30)
