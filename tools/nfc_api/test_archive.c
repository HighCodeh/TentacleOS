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

#include "nfc_archive.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool fail_save;
int storage_write_atomic(const char *path,const void *data,size_t length) {
  if(fail_save) return ESP_FAIL;
  FILE *file=fopen(path,"wb");assert(file);
  assert(fwrite(data,1,length,file)==length);assert(fclose(file)==0);return ESP_OK;
}
static void put(const char *path,const char *text,size_t length) {
  FILE *file=fopen(path,"wb");assert(file);assert(fwrite(text,1,length,file)==length);fclose(file);
}
int main(int argc,char **argv) {
  assert(argc==2);
  const char *families[]={"Mifare DESFire","FeliCa","ST25TB","NTAG4xx","NDEF","Texkom",
                          "ISO14443-3A","ISO14443-3B","ISO14443-4A","ISO14443-4B"};
  char source[256],path[NFC_ARCHIVE_PATH_SIZE],previous[NFC_ARCHIVE_PATH_SIZE],text[2048];
  snprintf(source,sizeof(source),"%s/source.nfc",argv[1]);
  for(size_t i=0;i<sizeof(families)/sizeof(families[0]);i++) {
    int length=snprintf(text,sizeof(text),"# original metadata\r\nFiletype: Flipper NFC device\r\nVersion: 4\r\nDevice type: %s\r\nUID: 01 02 03 04\r\nUnknown future field: keep me\r\nApplication 123456 File 01: 00 AA 55 FF\r\n",families[i]);
    put(source,text,(size_t)length);
    nfc_archive_t archive={0},copy={0};
    assert(nfc_archive_load(source,&archive)==ESP_OK);
    assert(!archive.is_capture && !strcmp(archive.device,families[i]));
    assert(nfc_archive_export(&archive,path,sizeof(path))==ESP_OK);
    strcpy(previous,path);
    assert(nfc_archive_load(path,&copy)==ESP_OK);
    assert(copy.length==(size_t)length && !memcmp(text,copy.data,copy.length));
    nfc_archive_free(&copy);
    assert(nfc_archive_export(&archive,path,sizeof(path))==ESP_OK && strcmp(previous,path));
    fail_save=true;assert(nfc_archive_export(&archive,path,sizeof(path))==ESP_FAIL && !path[0]);fail_save=false;
    nfc_archive_free(&archive);assert(!archive.data && !archive.length);
  }
  const char *type4="Filetype: Flipper NFC device\nVersion: 4\nDevice type: Type 4 Tag\nUID: 01 02 03 04\nNDEF Data Size: 4\nNDEF Data: 01 02\nNDEF Data: 03 04\n";
  nfc_archive_t t4={0};put(source,type4,strlen(type4));
  assert(nfc_archive_load(source,&t4)==ESP_OK);
  assert(nfc_archive_export(&t4,path,sizeof(path))==ESP_OK);
  nfc_archive_free(&t4);
  const char *invalid[]={
    "Filetype: Flipper NFC device\nVersion: 4\nDevice type: ST25TB\nUID: GG\nX: a\n",
    "Filetype: Flipper NFC device\nVersion: 4\nDevice type: ST25TB\nUID: 01\nUID: 02\n",
    "Filetype: Flipper NFC device\nVersion: 4\nDevice type: ST25TB\nUID: 01\nX: a\nX: b\n",
    "Filetype: Flipper NFC device\nVersion: 3\nDevice type: ST25TB\nUID: 01\nX: a\n",
    "Filetype: Flipper NFC device\nVersion: 4\nDevice type: ST25TB\nUID: 01\nnot a field\n",
    "Filetype: Flipper NFC device\nVersion: 4\nDevice type: ST25TB\nX: a\n"};
  for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
    nfc_archive_t archive={0};put(source,invalid[i],strlen(invalid[i]));
    assert(nfc_archive_load(source,&archive)!=ESP_OK && !archive.data);
  }
  nfc_archive_t archive={0};
  text[20]=0;put(source,text,strlen(text)+12);assert(nfc_archive_load(source,&archive)!=ESP_OK);
  uint8_t uid[]={1,2,3,4},data[]={0,255,1,2};
  nfc_archive_field_t fields[]={{"Binary data",data,sizeof(data)},{"Known empty",NULL,0}};
  assert(nfc_archive_capture("NDEF",uid,sizeof(uid),fields,2,path,sizeof(path))==ESP_OK);
  assert(nfc_archive_load(path,&archive)==ESP_OK && archive.is_capture);
  assert(strstr(archive.data,"Binary data: 00 FF 01 02\n") && strstr(archive.data,"Known empty:\n"));
  assert(strstr(archive.data,"Copyright (c) 2025 HIGH CODE LLC"));
  nfc_archive_free(&archive);
  fields[0].name="Injected\nUID";
  assert(nfc_archive_capture("NDEF",uid,sizeof(uid),fields,2,path,sizeof(path))!=ESP_OK);
  fields[0].name="UID";
  assert(nfc_archive_capture("NDEF",uid,sizeof(uid),fields,2,path,sizeof(path))!=ESP_OK);
  puts("NFC archive: byte-identical cross-family copies, captured fields, malformed inputs, save failures and no overwrite passed");
}
