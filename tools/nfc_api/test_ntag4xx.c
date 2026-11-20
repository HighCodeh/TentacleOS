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

#include "nfc_ntag4xx.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t s_response[32];
static size_t s_length;
static uint16_t s_status;
static esp_err_t s_error;
static bool s_save_failure;

int mf_desfire_transceive_native(mf_desfire_session_t *session, uint8_t command,
    const uint8_t *data, size_t size, uint8_t *out, size_t capacity, size_t *length, uint16_t *status) {
  assert(session && command == 0x60 && !data && !size);
  if (s_error) return s_error;
  assert(s_length <= capacity);
  memcpy(out, s_response, s_length); *length=s_length; *status=s_status;
  return ESP_OK;
}
int storage_write_atomic(const char *path,const void *data,size_t size) {
  if (s_save_failure) return ESP_FAIL;
  FILE *file=fopen(path,"wb");assert(file);
  assert(fwrite(data,1,size,file)==size);fclose(file);return ESP_OK;
}
static void write_text(const char *path,const char *data) {
  FILE *file=fopen(path,"wb");assert(file);fputs(data,file);fclose(file);
}
static const char FIXTURE[] =
  "Filetype: Flipper NFC device\nVersion: 4\nDevice type: NTAG4xx\n"
  "UID: 04 01 02 03 04 05 06\nATQA: 03 44\nSAK: 20\n"
  "T0: 78\nTA(1): 77\nTB(1): 81\nTC(1): 02\nT1...Tk: 80 00\n"
  "PICC Version: 04 04 08 30 00 11 05 04 04 08 01 00 11 05 "
  "04 01 02 03 04 05 06 AA BB CC DD E5 93 24 42\n";
int main(int argc,char **argv) {
  assert(argc==2);const char *path=argv[1];
  nfc_ntag4xx_card_t card={0},loaded={0};
  write_text(path,FIXTURE);assert(nfc_ntag4xx_load(path,&card)==ESP_OK);
  assert(!strcmp(nfc_ntag4xx_name(&card),"NTAG424 DNA TagTamper"));
  assert(card.identity.atqa[0]==0x44 && card.identity.atqa[1]==3);
  assert(card.identity.ats_len==7 && card.identity.ats[6]==0);
  assert(card.version[28]==0x42 && !card.has_ndef);
  card.has_cc=true;card.has_ndef=true;card.ndef_length=sizeof(card.ndef);
  for(size_t i=0;i<sizeof(card.ndef);i++)card.ndef[i]=(uint8_t)i;
  memset(card.cc,0x42,sizeof(card.cc));
  assert(nfc_ntag4xx_save(path,&card)==ESP_OK);assert(nfc_ntag4xx_load(path,&loaded)==ESP_OK);
  assert(!memcmp(&card,&loaded,sizeof(card)));
  char page[512];for(size_t i=0;i<NFC_NTAG4XX_PAGES;i++){nfc_ntag4xx_page(&card,i,page,sizeof(page));assert(*page);}
  nfc_ntag4xx_page(&card,4,page,sizeof(page));assert(strstr(page,"AABBCCDDE"));assert(strstr(page,"Fab key 0B ID 42"));
  card.ndef_length=0;assert(nfc_ntag4xx_save(path,&card)==ESP_OK);
  assert(nfc_ntag4xx_load(path,&loaded)==ESP_OK && loaded.has_ndef && !loaded.ndef_length);
  s_save_failure=true;assert(nfc_ntag4xx_save(path,&card)==ESP_FAIL);s_save_failure=false;
  card.identity.ats[1]=0x79;assert(nfc_ntag4xx_save(path,&card)==ESP_ERR_INVALID_ARG);card.identity.ats[1]=0x78;
  const char *bad[]={"\nSAK: 20\n","\nHighboy NDEF: 01\n","\nPICC Version: 00\n","\nATQA: FF\n"};
  for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
    write_text(path,FIXTURE);FILE *file=fopen(path,"ab");assert(file);fputs(bad[i],file);fclose(file);
    nfc_ntag4xx_card_t before=loaded;
    assert(nfc_ntag4xx_load(path,&loaded)==ESP_ERR_INVALID_RESPONSE);assert(!memcmp(&before,&loaded,sizeof(before)));
  }
  char invalid[sizeof(FIXTURE)];memcpy(invalid,FIXTURE,sizeof(invalid));
  char *ta=strstr(invalid,"TA(1)");assert(ta);ta[0]='X';write_text(path,invalid);
  assert(nfc_ntag4xx_load(path,&loaded)==ESP_ERR_INVALID_RESPONSE);
  memcpy(invalid,FIXTURE,sizeof(invalid));char *type=strstr(invalid,"04 04 08 30");assert(type);type[4]='1';write_text(path,invalid);
  assert(nfc_ntag4xx_load(path,&loaded)==ESP_ERR_INVALID_RESPONSE);
  mf_desfire_session_t session={0};session.dep.ats_len=card.identity.ats_len;
  memcpy(session.dep.ats,card.identity.ats,session.dep.ats_len);
  nfc_iso14443a_data_t identity={.uid_len=7,.sak=0x20};memcpy(identity.uid,card.identity.uid,7);
  memcpy(identity.atqa,card.identity.atqa,2);memcpy(s_response,card.version,29);s_status=0x9100;
  for(size_t n=27;n<=30;n++) {
    s_length=n;esp_err_t result=nfc_ntag4xx_read(&session,&identity,&loaded);
    assert(result==((n==28||n==29)?ESP_OK:ESP_ERR_INVALID_RESPONSE));
    if(result==ESP_OK)assert(loaded.version[28]==(n==28?0:0x42));
  }
  s_length=29;s_status=0x91ae;assert(nfc_ntag4xx_read(&session,&identity,&loaded)==ESP_ERR_INVALID_RESPONSE);
  s_status=0x9100;s_error=ESP_ERR_TIMEOUT;assert(nfc_ntag4xx_read(&session,&identity,&loaded)==ESP_ERR_TIMEOUT);s_error=0;
  s_response[1]=0x14;s_response[3]=0x10;
  assert(nfc_ntag4xx_read(&session,&identity,&loaded)==ESP_OK);assert(!strcmp(nfc_ntag4xx_name(&loaded),"NTAG413 DNA"));
  s_response[3]=0x30;s_response[2]=0;assert(nfc_ntag4xx_read(&session,&identity,&loaded)==ESP_OK);
  assert(!strcmp(nfc_ntag4xx_name(&loaded),"NTAG424 DNA"));
  s_response[3]=0xff;assert(nfc_ntag4xx_read(&session,&identity,&loaded)==ESP_OK);
  assert(!strcmp(nfc_ntag4xx_name(&loaded),"Unknown NTAG4xx"));
  puts("NTAG4xx: version lengths, models, fork files, ATS, NDEF preservation and rejection paths passed");
  return 0;
}
