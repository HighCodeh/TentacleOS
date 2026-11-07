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


#include "nfc_emv.h"
#include "nfc_apdu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned s_calls;
static unsigned s_fail_at;
static unsigned s_cancel_at;
static bool s_long_gpo;
static bool s_name_fallback;
static const uint8_t s_aid[] = {0xA0,0,0,0,3,0x10,0x10};

int storage_write_atomic(const char *path, const void *data, size_t length) {
  FILE *file = fopen(path,"wb");
  assert(file);
  assert(fwrite(data,1,length,file)==length);
  assert(!fclose(file));
  return ESP_OK;
}
size_t nfc_apdu_build_select_aid(nfc_apdu_buf_t dst, nfc_apdu_data_t aid, bool le_present, uint8_t le) {
  assert(aid.len+6<=dst.max && le_present);
  const uint8_t header[]={0,0xA4,4,0};
  memcpy(dst.buf,header,4);dst.buf[4]=aid.len;
  memcpy(dst.buf+5,aid.ptr,aid.len);dst.buf[5+aid.len]=le;
  return aid.len+6;
}
static bool cancel(void) {return s_cancel_at && s_calls>=s_cancel_at;}
int nfc_apdu_transceive(const nfc_apdu_channel_t *channel,const uint8_t *apdu,size_t length,nfc_apdu_resp_t *response) {
  (void)channel;
  assert(length>=5);
  assert(apdu[1]==0xA4 || apdu[1]==0xA8 || apdu[1]==0xB2 || apdu[1]==0xCA);
  s_calls++;
  if(s_fail_at==s_calls) {response->sw=0x6985;return ESP_ERR_INVALID_RESPONSE;}
  if(s_long_gpo) {
    assert(apdu[1]==0xA8 && length==137 && apdu[4]==131 && apdu[5]==0x83 && apdu[6]==0x81 && apdu[7]==128);
    response->sw=0x9000;return ESP_OK;
  }
  /* A valid FCI with a required four-byte PDOL. */
  static const uint8_t select[]={0x6F,0x0B,0x50,3,'T','E','S',0x9F,0x38,3,0x9F,0x66,4};
  static const uint8_t gpo[]={0x77,0x0A,0x82,2,0x20,0,0x94,4,0x10,1,1,0};
  static const uint8_t record[]={0x70,0x18,0x5A,4,0x12,0x34,0x56,0x78,0x5F,0x20,4,'T','E','S','T',0x5F,0x24,3,0x29,0x12,0x31,0x9F,0x4D,2,4,1};
  static const uint8_t atc[]={0x9F,0x36,2,0,7};
  static const uint8_t last[]={0x9F,0x13,2,0,5};
  static const uint8_t pin[]={0x9F,0x17,1,3};
  static const uint8_t log_format[]={0x9F,0x4F,8,0x9F,0x36,2,0x9F,0x02,6,0x9A,3};
  static const uint8_t log[]={0,7,0,0,0,0,0x12,0x34,0x26,0x11,6};

  const uint8_t *data=NULL;size_t size=0;
  switch(s_calls) {
    case 1: assert(apdu[1]==0xA4);data=select;size=sizeof(select);break;
    case 2: assert(apdu[1]==0xA8 && apdu[6]==4 && apdu[7]==0x79);data=gpo;size=sizeof(gpo);break;
    case 3: assert(apdu[1]==0xB2 && apdu[2]==1 && apdu[3]==0x14);data=record;size=sizeof(record);break;
    case 4: assert(apdu[1]==0xCA && apdu[3]==0x13);data=last;size=sizeof(last);break;
    case 5: assert(apdu[1]==0xCA && apdu[3]==0x17);data=pin;size=sizeof(pin);break;
    case 6: assert(apdu[1]==0xCA && apdu[3]==0x36);data=atc;size=sizeof(atc);break;
    case 7: assert(apdu[1]==0xCA && apdu[3]==0x4F);data=log_format;size=sizeof(log_format);break;
    case 8: assert(apdu[1]==0xB2 && apdu[3]==0x24);data=log;size=sizeof(log);break;
    case 9: {
      static const uint8_t holder[]={0x70,7,0x5F,0x20,4,'N','A','M','E'};
      assert(s_name_fallback && apdu[1]==0xB2 && apdu[2]==2 && apdu[3]==0x14);
      data=holder;size=sizeof(holder);break;
    }
    default: assert(false);
  }
  assert(size<=response->max);memcpy(response->buf,data,size);response->len=size;response->sw=0x9000;
  if(s_name_fallback && s_calls==3) response->buf[9]=0x21;
  return ESP_OK;
}
static emv_profile_t profile(void) {
  emv_profile_t result={0};
  memcpy(result.app.aid,s_aid,sizeof(s_aid));result.app.aid_len=sizeof(s_aid);
  return result;
}
static void test_parsers(void) {
  emv_app_t apps[2];
  const uint8_t ppse[]={0x61,10,0x50,1,'A',0x4F,5,0xA0,0,0,0,3,0x61,7,0x4F,5,0xA0,0,0,0,4};
  assert(emv_extract_aids(ppse,sizeof(ppse),apps,2)==2);
  assert(!strcmp(apps[0].label,"A") && !apps[1].label[0]);
  assert(emv_extract_aids(ppse,sizeof(ppse)-1,apps,2)==0);
  const uint8_t dol[]={0x9F,0x66,5,0xDF,1,2};
  uint8_t out[256];size_t size=123;
  assert(emv_prepare_pdol(dol,sizeof(dol),out,sizeof(out),&size)==ESP_OK && size==7);
  assert(out[0]==0x79 && out[3]==0x80 && !out[4] && !out[5] && !out[6]);
  assert(emv_prepare_pdol(dol,sizeof(dol)-1,out,sizeof(out),&size)!=ESP_OK && size==0);
  assert(emv_prepare_pdol(dol,sizeof(dol),out,6,&size)!=ESP_OK);
  const uint8_t amount_dol[]={0x9F,0x02,6};
  assert(emv_prepare_pdol(amount_dol,sizeof(amount_dol),out,sizeof(out),&size)==ESP_OK && size==6 && out[3]==0x10);
  uint8_t afl[]={0x80,6,0x20,0,0x10,1,2,0};emv_afl_entry_t entries[2];
  assert(emv_parse_afl(afl,sizeof(afl),entries,2)==1 && entries[0].sfi==2);
  afl[4]=0;assert(!emv_parse_afl(afl,sizeof(afl),entries,2));afl[4]=0x11;
  assert(!emv_parse_afl(afl,sizeof(afl),entries,2));afl[4]=0x10;afl[5]=3;
  assert(!emv_parse_afl(afl,sizeof(afl),entries,2));
  emv_profile_t card=profile(),before=card;
  const uint8_t invalid[]={0x50,1,'X',0x82,1,0};
  assert(emv_decode_metadata(invalid,sizeof(invalid),&card)!=ESP_OK && !memcmp(&card,&before,sizeof(card)));
  const uint8_t track[]={0x57,6,0x12,0x34,0x5D,0x29,0x12,0xFF};
  assert(emv_decode_metadata(track,sizeof(track),&card)==ESP_OK);
  assert(card.length[EMV_FIELD_PAN]==3 && card.value[EMV_FIELD_PAN][2]==0x5F);
  assert(card.value[EMV_FIELD_EXPIRY][0]==0x29 && card.value[EMV_FIELD_EXPIRY][1]==0x12);
  uint16_t sw;s_long_gpo=true;
  assert(emv_get_processing_options(0,NULL,out,128,out,sizeof(out),&size,&sw,10)==ESP_OK);
  assert(emv_get_processing_options(0,NULL,out,248,out,sizeof(out),&size,&sw,10)!=ESP_OK);
  s_long_gpo=false;
}
static void test_workflow(emv_profile_t *out) {
  *out=profile();s_calls=0;
  assert(emv_read_application(0,NULL,out,cancel,10)==ESP_OK);
  assert(s_calls==8 && out->is_complete && out->records_read==1);
  assert(out->length[EMV_FIELD_PAN]==4 && !strcmp((char *)out->value[EMV_FIELD_HOLDER],"TEST"));
  assert(out->value[EMV_FIELD_ATC][1]==7 && out->log_count==1 && out->logs[0].value[1][5]==0x34);
  for(unsigned i=1;i<=3;i++) {
    emv_profile_t partial=profile();s_calls=0;s_fail_at=i;
    assert(emv_read_application(0,NULL,&partial,cancel,10)!=ESP_OK && !partial.is_complete && s_calls==i);
  }
  for(unsigned i=4;i<=7;i++) {
    emv_profile_t optional=profile();s_calls=0;s_fail_at=i;
    assert(emv_read_application(0,NULL,&optional,cancel,10)==ESP_OK && optional.is_complete);
  }
  s_fail_at=0;s_calls=0;s_name_fallback=true;
  emv_profile_t named=profile();
  assert(emv_read_application(0,NULL,&named,cancel,10)==ESP_OK && s_calls==9);
  assert(!strcmp((char *)named.value[EMV_FIELD_HOLDER],"NAME"));
  s_name_fallback=false;
  s_fail_at=0;s_calls=0;s_cancel_at=2;emv_profile_t partial=profile();
  assert(emv_read_application(0,NULL,&partial,cancel,10)==ESP_ERR_INVALID_STATE && s_calls==2);
  s_cancel_at=0;
}
static void test_file(const char *path,emv_profile_t *profile) {
  nfc_emv_card_t card={.identity={.uid={1,2,3,4},.uid_len=4,.sak=0x20,.ats={2,8},.ats_len=2},.profile=*profile};
  nfc_emv_card_t loaded;
  memcpy(card.profile.value[EMV_FIELD_HOLDER], " A  ", 4);
  card.profile.value[EMV_FIELD_COUNTRY][0] = 0x08;
  card.profile.value[EMV_FIELD_COUNTRY][1] = 0x40;
  card.profile.length[EMV_FIELD_COUNTRY] = 2;
  assert(nfc_emv_save(path,&card)==ESP_OK && nfc_emv_load(path,&loaded)==ESP_OK);
  assert(!memcmp(card.profile.value,loaded.profile.value,sizeof(profile->value)));
  assert(!memcmp(card.profile.length,loaded.profile.length,sizeof(profile->length)));
  assert(loaded.profile.log_count==1 && !memcmp(card.profile.logs,loaded.profile.logs,sizeof(profile->logs)));
  FILE *file=fopen(path,"rb");assert(file);
  char text[8192];size_t length=fread(text,1,sizeof(text)-1,file);fclose(file);text[length]=0;
  assert(strstr(text,"Country code: 40 08\n"));
  char *extension=strstr(text,"Highboy lengths:");assert(extension);*extension=0;
  assert(storage_write_atomic(path,text,strlen(text))==ESP_OK);
  assert(nfc_emv_load(path,&loaded)==ESP_OK && loaded.profile.is_complete && !loaded.profile.log_count);
  strcat(text,"PAN length: 4\n");assert(storage_write_atomic(path,text,strlen(text))==ESP_OK);
  nfc_emv_card_t previous=loaded;
  assert(nfc_emv_load(path,&loaded)!=ESP_OK && !memcmp(&previous,&loaded,sizeof(loaded)));
  card.profile= (emv_profile_t){.app=profile->app};
  assert(nfc_emv_save(path,&card)==ESP_OK && nfc_emv_load(path,&loaded)==ESP_OK);
  assert(!loaded.profile.is_complete && !loaded.profile.length[EMV_FIELD_AIP] && !loaded.profile.length[EMV_FIELD_PIN_TRIES]);
  card.profile.length[EMV_FIELD_PAN]=1;card.profile.value[EMV_FIELD_PAN][0]=0xAB;
  assert(nfc_emv_save(path,&card)==ESP_ERR_INVALID_ARG);
  card.profile=*profile;card.profile.length[EMV_FIELD_AIP]=1;
  assert(nfc_emv_save(path,&card)==ESP_ERR_INVALID_ARG);
}
int main(int argc,char **argv) {
  assert(argc==2);test_parsers();emv_profile_t card;test_workflow(&card);test_file(argv[1],&card);
  puts("EMV parser, workflow, cancellation and file tests passed");return 0;
}
