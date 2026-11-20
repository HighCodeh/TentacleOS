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

#include "nfc_library.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nvs.h"

#define TEST_RECORD_SIZE 2200
static const char *TAG = "NFC_LIBRARY_TEST";
static uint8_t committed[NFC_STORE_MAX_ENTRIES][TEST_RECORD_SIZE];
static uint8_t pending[NFC_STORE_MAX_ENTRIES][TEST_RECORD_SIZE];
static size_t committed_size[NFC_STORE_MAX_ENTRIES], pending_size[NFC_STORE_MAX_ENTRIES];
static bool is_mounted = true, fail_commit, fail_open, fail_read;
static char fail_delete[192], fail_move[192], fail_file_open[192];
int fail_alloc;

bool vfs_sdcard_is_mounted(void) { return is_mounted; }
int nvs_open(const char *name,int mode,int *out) {
  if(fail_open)return ESP_FAIL;
  assert(!strcmp(name,"nfc_vault"));*out=mode?2:1;
  if(mode){memcpy(pending,committed,sizeof(pending));memcpy(pending_size,committed_size,sizeof(pending_size));}
  return ESP_OK;
}
void nvs_close(int handle) { (void)handle; }
static int slot(const char *key) { int n=-1;assert(sscanf(key,"c%d",&n)==1 && n>=0 && n<NFC_STORE_MAX_ENTRIES);return n; }
int nvs_get_blob(int handle,const char *key,void *data,size_t *size) {
  if(fail_read)return ESP_FAIL;
  int n=slot(key);size_t length=handle==2?pending_size[n]:committed_size[n];
  if(!length)return ESP_ERR_NVS_NOT_FOUND;
  if(data){assert(*size>=length);memcpy(data,handle==2?pending[n]:committed[n],length);}
  *size=length;return ESP_OK;
}
int nvs_set_blob(int handle,const char *key,const void *data,size_t size) {
  assert(handle==2 && size<=TEST_RECORD_SIZE);int n=slot(key);memcpy(pending[n],data,size);pending_size[n]=size;return ESP_OK;
}
int nvs_erase_key(int handle,const char *key) {assert(handle==2);pending_size[slot(key)]=0;return ESP_OK;}
int nvs_commit(int handle) {
  assert(handle==2);if(fail_commit)return ESP_FAIL;
  memcpy(committed,pending,sizeof(committed));memcpy(committed_size,pending_size,sizeof(committed_size));return ESP_OK;
}
int storage_write_atomic(const char *path,const void *data,size_t length) {
  FILE *file=fopen(path,"wb");assert(file);assert(fwrite(data,1,length,file)==length);return fclose(file)==0?ESP_OK:ESP_FAIL;
}
int fault_unlink(const char *path) {if(fail_delete[0] && strstr(path,fail_delete)){errno=EIO;return -1;}return unlink(path);}
FILE *fault_fopen(const char *path,const char *mode) {if(fail_file_open[0] && strstr(path,fail_file_open)){errno=EIO;return NULL;}return fopen(path,mode);}
int fault_rename(const char *a,const char *b) {if(fail_move[0] && strstr(a,fail_move)){errno=EIO;return -1;}return rename(a,b);}
static void path_for(const char *name,char *path) {snprintf(path,NFC_ARCHIVE_PATH_SIZE,NFC_ARCHIVE_DIRECTORY "/%s",name);}
static bool exists(const char *name) {char path[NFC_ARCHIVE_PATH_SIZE];path_for(name,path);return access(path,F_OK)==0;}
static void put(const char *name,const void *bytes,size_t size) {
  char path[NFC_ARCHIVE_PATH_SIZE];path_for(name,path);FILE *file=fopen(path,"wb");assert(file);
  assert(fwrite(bytes,1,size,file)==size);assert(fclose(file)==0);
}
static void profile(const char *name,const char *device,const uint8_t *uid,size_t count) {
  char text[512];int used=snprintf(text,sizeof(text),"Filetype: Flipper NFC device\nVersion: 4\nDevice type: %s\nUID:",device);
  for(size_t i=0;i<count;i++)used+=snprintf(text+used,sizeof(text)-(size_t)used," %02X",uid[i]);
  used+=snprintf(text+used,sizeof(text)-(size_t)used,"\nUnknown field: retained exactly\n");
  put(name,text,(size_t)used);
}
static void binary(const char *name,const uint8_t *uid,size_t length) {
  uint8_t bytes[1024]={0};memcpy(bytes,uid,length);put(name,bytes,sizeof(bytes));
}
static void save(const char *name,const uint8_t *uid,size_t length,hb_nfc_protocol_t protocol) {
  const uint8_t payload[]={0,1,2,0xff,3,4};
  nfc_store_card_t card={.name=name,.uid=uid,.uid_len=length,.protocol=protocol,.payload=payload,.payload_len=sizeof(payload)};
  assert(nfc_store_save(&card)==HB_NFC_OK);
}
static int index_for(const char *name) {
  for(int i=0;i<nfc_store_count();i++){nfc_store_info_t info;assert(nfc_store_get_info(i,&info)==HB_NFC_OK);if(!strcmp(info.name,name))return i;}
  return -1;
}
static nfc_store_info_t info_for(const char *name) {nfc_store_info_t info;assert(nfc_store_get_info(index_for(name),&info)==HB_NFC_OK);return info;}
static void clear_vault(void){memset(committed_size,0,sizeof(committed_size));}
int main(void) {
  (void)TAG;assert(mkdir(NFC_ARCHIVE_DIRECTORY,0700)==0);nfc_store_init();
  const uint8_t a[]={1,2,3,4,5,6,7},b[]={1,2,3,4,5,6,8};
  save("Office",a,sizeof(a),HB_PROTO_MF_CLASSIC);save("Office alias",a,sizeof(a),HB_PROTO_MF_CLASSIC);
  save("Other",b,sizeof(b),HB_PROTO_MF_CLASSIC);save("Plus",a,sizeof(a),HB_PROTO_MF_PLUS);
  profile("office.nfc","Mifare Classic",a,sizeof(a));binary("office.bin",a,sizeof(a));put("office.keys","key-data",8);
  profile("copy.nfc","Mifare Classic",a,sizeof(a));profile("other.nfc","Mifare Classic",b,sizeof(b));profile("plus.nfc","Mifare Plus",a,sizeof(a));
  nfc_store_info_t card=info_for("Office");is_mounted=false;
  assert(nfc_library_delete_card(&card)==ESP_ERR_INVALID_STATE && exists("office.nfc") && nfc_store_count()==4);is_mounted=true;
  fail_open=true;assert(nfc_library_delete_card(&card)!=ESP_OK && exists("office.nfc"));fail_open=false;
  fail_read=true;assert(nfc_library_delete_card(&card)!=ESP_OK && exists("office.bin"));fail_read=false;
  strcpy(fail_file_open,"office.nfc");assert(nfc_library_delete_card(&card)!=ESP_OK && exists("office.bin") && nfc_store_count()==4);fail_file_open[0]=0;
  fail_alloc=1;assert(nfc_library_delete_card(&card)==ESP_ERR_NO_MEM && exists("office.bin"));fail_alloc=0;
  strcpy(fail_delete,"office.bin");assert(nfc_library_delete_card(&card)!=ESP_OK);
  assert(exists("office.nfc") && exists("office.bin") && nfc_store_count()==4);fail_delete[0]=0;
  assert(nfc_library_delete_card(&card)==ESP_OK);
  assert(!exists("office.nfc") && !exists("office.bin") && !exists("office.keys") && !exists("copy.nfc"));
  assert(nfc_store_count()==2 && exists("other.nfc") && exists("plus.nfc"));
  bool card_deleted=false;
  assert(nfc_library_delete_file("other.nfc",&card_deleted)==ESP_OK && card_deleted && nfc_store_count()==1);
  assert(exists("plus.nfc"));
  assert(nfc_library_delete_file("plus.nfc",&card_deleted)==ESP_OK && card_deleted && nfc_store_count()==0);
  save("Office",a,sizeof(a),HB_PROTO_MF_CLASSIC);
  profile("office.nfc","Mifare Classic",a,sizeof(a));binary("office.bin",a,sizeof(a));put("office.keys","key-data",8);
  char old_path[NFC_ARCHIVE_PATH_SIZE];path_for("office.nfc",old_path);nfc_archive_t original={0};assert(nfc_archive_load(old_path,&original)==ESP_OK);
  fail_open=true;assert(nfc_library_rename_file("office.nfc","Work")!=ESP_OK && exists("office.nfc"));fail_open=false;
  fail_read=true;assert(nfc_library_rename_file("office.nfc","Work")!=ESP_OK && exists("office.nfc"));fail_read=false;
  binary("office.bin",b,sizeof(b));assert(nfc_library_rename_file("office.nfc","Work")!=ESP_OK && exists("office.nfc"));binary("office.bin",a,sizeof(a));
  fail_commit=true;assert(nfc_library_rename_file("office.nfc","Work")!=ESP_OK);
  assert(exists("office.nfc") && exists("office.bin") && !exists("Work.nfc") && index_for("Office")==0);fail_commit=false;
  strcpy(fail_move,"office.bin");assert(nfc_library_rename_file("office.nfc","Work")!=ESP_OK);
  assert(exists("office.nfc") && exists("office.bin") && !exists("Work.nfc"));fail_move[0]=0;
  put("Work.bin","occupied",8);assert(nfc_library_rename_file("office.nfc","Work")!=ESP_OK && exists("office.nfc"));
  char path[NFC_ARCHIVE_PATH_SIZE];path_for("Work.bin",path);assert(unlink(path)==0);
  assert(nfc_library_rename_file("office.nfc","Work")==ESP_OK);
  assert(!exists("office.nfc") && !exists("office.bin") && !exists("office.keys"));
  assert(exists("Work.nfc") && exists("Work.bin") && exists("Work.keys"));
  nfc_store_entry_t entry;assert(nfc_store_load(0,&entry)==HB_NFC_OK && !strcmp(entry.name,"Work"));
  const uint8_t payload[]={0,1,2,0xff,3,4};assert(entry.payload_len==sizeof(payload) && !memcmp(entry.payload,payload,sizeof(payload)));
  path_for("Work.nfc",path);nfc_archive_t renamed={0};assert(nfc_archive_load(path,&renamed)==ESP_OK);
  assert(original.length==renamed.length && !memcmp(original.data,renamed.data,original.length));nfc_archive_free(&original);nfc_archive_free(&renamed);
  assert(nfc_library_rename_file("Work.nfc","../escape")==ESP_ERR_INVALID_ARG);
  card=info_for("Work");fail_commit=true;assert(nfc_library_delete_card(&card)!=ESP_OK && index_for("Work")==0);
  fail_commit=false;assert(nfc_library_delete_card(&card)==ESP_OK && nfc_store_count()==0);
  put("broken.nfc","invalid",7);assert(nfc_library_delete_file("broken.nfc",&card_deleted)==ESP_OK && !card_deleted);
  // Full UID matching applies beyond the emulator's ten-file picker limit.
  save("Many",a,sizeof(a),HB_PROTO_MF_CLASSIC);card=info_for("Many");
  for(int i=0;i<20;i++){char name[32];snprintf(name,sizeof(name),"copy_%02d.nfc",i);profile(name,"Mifare Classic",a,sizeof(a));}
  assert(nfc_library_delete_card(&card)==ESP_OK);
  for(int i=0;i<20;i++){char name[32];snprintf(name,sizeof(name),"copy_%02d.nfc",i);assert(!exists(name));}
  const uint8_t v_wire[]={1,2,3,4,5,6,7,0xe0},v_file[]={0xe0,7,6,5,4,3,2,1};
  save("V card",v_wire,sizeof(v_wire),HB_PROTO_SLIX);
  profile("vicinity.nfc","ISO15693-3",v_file,sizeof(v_file));card=info_for("V card");
  assert(nfc_library_delete_card(&card)==ESP_OK && !exists("vicinity.nfc") && nfc_store_count()==0);
  save("Alias one",a,sizeof(a),HB_PROTO_MF_CLASSIC);save("Alias two",a,sizeof(a),HB_PROTO_MF_CLASSIC);
  profile("aliases.nfc","Mifare Classic",a,sizeof(a));
  assert(nfc_library_rename_file("aliases.nfc","Work")==ESP_ERR_INVALID_STATE && exists("aliases.nfc"));
  assert(nfc_library_delete_file("aliases.nfc",&card_deleted)==ESP_OK && card_deleted && nfc_store_count()==0);
  clear_vault();assert(rmdir(NFC_ARCHIVE_DIRECTORY)==0);
  puts("PASS NFC library: full UID/family cascade, all aliases/copies/companions, absent SD, deletion retry, lossless rename, NVS/SD rollback, payload retention and collisions");
}
