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

#include "nfc_felica.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static nfc_felica_listener_t remote;
static bool cancelled, corrupt, save_failure;
static unsigned exchanges;
static uint8_t first_poll_idm[8];

void esp_fill_random(void *data,size_t size) { for(size_t i=0;i<size;i++) ((uint8_t *)data)[i]=(uint8_t)i; }
int storage_write_atomic(const char *path,const void *data,size_t size) {
  if(save_failure) return ESP_FAIL;
  FILE *file=fopen(path,"wb"); if(!file) return ESP_FAIL;
  bool ok=fwrite(data,1,size,file)==size; fclose(file); return ok?ESP_OK:ESP_FAIL;
}
int felica_poller_init(void) {return ESP_OK;}
int felica_sensf_req(uint16_t system,felica_tag_t *tag) {
  uint8_t req[]={6,0,(uint8_t)(system>>8),(uint8_t)system,1,0}; nfc_felica_frame_t response;
  assert(nfc_felica_listener_frame(&remote,req,sizeof(req),&response)==ESP_OK);
  if(!response.size) return ESP_ERR_NOT_FOUND;
  memset(tag,0,sizeof(*tag));memcpy(tag->idm,response.data+2,8);memcpy(tag->pmm,response.data+10,8);
  memcpy(tag->rd,response.data+18,2);tag->rd_valid=true;
  if(system==0xffff) memcpy(first_poll_idm,tag->idm,8);
  return ESP_OK;
}
int nfc_poller_transceive(const uint8_t *tx,size_t size,bool crc,uint8_t *rx,size_t cap,int bits,int timeout) {
  (void)crc;(void)bits;(void)timeout;exchanges++;
  nfc_felica_frame_t response;
  assert(nfc_felica_listener_frame(&remote,tx,size,&response)==ESP_OK);
  assert(response.size<=cap);
  memcpy(rx,response.data,response.size);
  if(corrupt && response.size) rx[1]^=1;
  return (int)response.size;
}
static bool should_cancel(void){return cancelled;}
static nfc_felica_block_t *block(nfc_felica_card_t *card,unsigned address){int i=nfc_felica_lite_index(address);assert(i>=0);return &card->lite[i];}
static void make_lite(nfc_felica_card_t *card) {
  memset(card,0,sizeof(*card));card->type=NFC_FELICA_TYPE_LITE;card->tag.pmm[1]=0xf1;
  for(size_t i=0;i<8;i++) card->tag.idm[i]=(uint8_t)(i+1);
  card->blocks_total=28;card->blocks_read=28;card->has_key=true;
  for(size_t i=0;i<28;i++) for(size_t j=0;j<16;j++) card->lite[i].data[j]=(uint8_t)(i+j);
  memset(block(card,0x88)->data,0,16);block(card,0x88)->data[0]=0xff;block(card,0x88)->data[1]=0xff;
  block(card,0x88)->data[2]=0xff;block(card,0x88)->data[12]=1;
  memset(block(card,0x90)->data,0,16);memset(block(card,0x92)->data,0,16);
}
static nfc_felica_frame_t request(nfc_felica_listener_t *listener,uint8_t command,
                                 const uint8_t *payload,size_t length) {
  uint8_t tx[255]={0};tx[0]=(uint8_t)(10+length);tx[1]=command;memcpy(tx+2,listener->card->tag.idm,8);
  if(listener->card->type==NFC_FELICA_TYPE_STANDARD)tx[2]=(tx[2]&15)|(listener->system<<4);
  memcpy(tx+10,payload,length);nfc_felica_frame_t response;
  assert(nfc_felica_listener_frame(listener,tx,10+length,&response)==ESP_OK);return response;
}
static void make_standard(nfc_felica_card_t *card) {
  memset(card,0,sizeof(*card));card->type=NFC_FELICA_TYPE_STANDARD;card->tag.pmm[1]=0x32;
  memcpy(card->tag.idm,"\x01\x02\x03\x04\x05\x06\x07\x08",8);
  card->system_count=2;card->systems=calloc(2,sizeof(*card->systems));assert(card->systems);
  for(size_t i=0;i<2;i++) {
    nfc_felica_system_t *s=&card->systems[i];s->code=(uint16_t)(3+i);s->key_version=0x1234;
    s->area_count=1;s->areas=calloc(1,sizeof(*s->areas));assert(s->areas);
    s->areas[0]=(nfc_felica_area_t){0,0xfffe,0,1,0x3456};
    s->service_count=2;s->services=calloc(2,sizeof(*s->services));assert(s->services);
    s->services[0]=(nfc_felica_service_t){9,0x4567};s->services[1]=(nfc_felica_service_t){11,0x5678};
    s->block_count=3;s->blocks=calloc(3,sizeof(*s->blocks));assert(s->blocks);
    for(size_t j=0;j<3;j++){s->blocks[j].service=j==2?11:9;s->blocks[j].index=j==2?0:(uint8_t)j;
      memset(s->blocks[j].block.data,(int)(1+i*10+j),16);}
  }
}
static void check_file(const char *path,nfc_felica_card_t *card) {
  assert(nfc_felica_save(path,card)==ESP_OK);nfc_felica_card_t loaded={0};
  assert(nfc_felica_load(path,&loaded)==ESP_OK);assert(loaded.type==card->type);
  assert(!memcmp(loaded.tag.idm,card->tag.idm,8));assert(loaded.has_key==card->has_key);
  assert(!loaded.is_external_auth && !loaded.is_internal_auth);
  assert(loaded.system_count==card->system_count);assert(loaded.blocks_total==card->blocks_total);
  if(card->type==NFC_FELICA_TYPE_LITE)assert(!memcmp(loaded.lite,card->lite,sizeof(card->lite)));
  for(size_t i=0;i<card->system_count;i++){
    assert(loaded.systems[i].service_count==card->systems[i].service_count);
    assert(loaded.systems[i].area_count==card->systems[i].area_count);
    assert(loaded.systems[i].block_count==card->systems[i].block_count);
    assert(!memcmp(loaded.systems[i].blocks,card->systems[i].blocks,card->systems[i].block_count*sizeof(*card->systems[i].blocks)));
  }
  char page[512];for(size_t i=0;i<nfc_felica_page_count(&loaded);i++){nfc_felica_page(&loaded,i,page,sizeof(page));assert(page[0]);}
  nfc_felica_free(&loaded);
  FILE *file=fopen(path,"ab");assert(file);fputs("\nPublic blocks read: 0\n",file);fclose(file);
  if(card->system_count) assert(nfc_felica_load(path,&loaded)==ESP_ERR_INVALID_RESPONSE);
  nfc_felica_free(&loaded);
  save_failure=true;assert(nfc_felica_save(path,card)==ESP_FAIL);save_failure=false;
}
static void malformed(nfc_felica_listener_t *listener) {
  uint8_t frame[]={16,6,1,2,3,4,5,6,7,8,1,11,0,1,0x80,0};nfc_felica_frame_t response;
  for(size_t n=0;n<sizeof(frame);n++) assert(nfc_felica_listener_frame(listener,frame,n,&response)==ESP_ERR_INVALID_RESPONSE);
  frame[0]=15;assert(nfc_felica_listener_frame(listener,frame,sizeof(frame),&response)==ESP_ERR_INVALID_RESPONSE);
  frame[0]=16;frame[14]=0;assert(nfc_felica_listener_frame(listener,frame,sizeof(frame),&response)==ESP_ERR_INVALID_RESPONSE);
  frame[14]=0x81;assert(nfc_felica_listener_frame(listener,frame,sizeof(frame),&response)==ESP_OK);assert(response.data[11]==0xa3);
  frame[14]=0x90;assert(nfc_felica_listener_frame(listener,frame,sizeof(frame),&response)==ESP_OK);assert(response.data[11]==0xa7);
}
static void crypto_vectors(void) {
  /* Independently generated with Python cryptography TripleDES-CBC. */
  const uint8_t session[]={0x40,0xbd,0xa1,0x06,0x77,0x88,0xa1,0x17,0x7b,0x65,0x9d,0x67,0x8e,0x4b,0xc6,0x38};
  const uint8_t read_mac[]={0xc7,0xa6,0xf0,0xd1,0x5a,0xd9,0x12,0xd4};
  const uint8_t write_mac[]={0xab,0x24,0x3c,0x1f,0x32,0xa8,0x83,0xf5};
  uint8_t key[16],rc[16],out[8];for(size_t i=0;i<16;i++){key[i]=i;rc[i]=16+i;}
  nfc_felica_crypto_t crypto;
  assert(nfc_felica_crypto_init(&crypto,key,rc)==ESP_OK);assert(!memcmp(crypto.key,session,16));
  nfc_felica_frame_t data={.size=32};for(size_t i=0;i<32;i++)data.data[i]=i;
  uint8_t blocks[]={0x82,0x90,0x91};
  assert(nfc_felica_mac_read(&crypto,blocks,&data,out)==ESP_OK);assert(!memcmp(out,read_mac,8));
  uint8_t counter[]={0x34,0x12,0},state[16]={1};
  assert(nfc_felica_mac_write(&crypto,counter,state,out)==ESP_OK);assert(!memcmp(out,write_mac,8));
  data.size=1;assert(nfc_felica_mac_read(&crypto,blocks,&data,out)==ESP_ERR_INVALID_ARG);
}

static void legacy_files(const char *path) {
  for (unsigned version=2;version<=4;version++) {
    FILE *file=fopen(path,"wb");assert(file);
    fprintf(file,"Filetype: Flipper NFC device\nVersion: 4\nDevice type: FeliCa\n"
      "UID: 01 02 03 04 05 06 07 08\nData format version: %u\n"
      "Manufacture id: 01 02 03 04 05 06 07 08\n"
      "Manufacture parameter: 00 32 00 00 00 00 00 00\n"
      "System found: 1\nSystem 00: 0003%s\nArea found: 0\nService found: 1\n"
      "Service 000: | Code 000B | Attrib. 0B |%s\nPublic blocks read: 1\n"
      "Block 0000: | Service code 000B | Block index 00 | Data: "
      "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F |\n",
      version,version>=3?" | Key version 1234 |":"",version>=3?" Key version 5678 |":"");
    fclose(file);
    nfc_felica_card_t card={0};assert(nfc_felica_load(path,&card)==ESP_OK);
    assert(card.system_count==1 && card.systems[0].block_count==1);
    assert(card.systems[0].key_version==(version==4?0x1234:0xffff));
    assert(card.systems[0].blocks[0].block.data[15]==15);nfc_felica_free(&card);
  }
  FILE *file=fopen(path,"wb");assert(file);
  fputs("Filetype: Flipper NFC device\nVersion: 4\nDevice type: FeliCa\n"
    "UID: 01 02 03 04 05 06 07 08\nData format version: 1\n"
    "Manufacture id: 01 02 03 04 05 06 07 08\n"
    "Manufacture parameter: 00 F1 00 00 00 00 00 00\n"
    "Blocks total: 1\nBlocks read: 1\n"
    "Block 0: 00 00 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n",file);
  fclose(file);
  nfc_felica_card_t card={0};assert(nfc_felica_load(path,&card)==ESP_OK);
  assert(!card.has_key && !card.lite[0].sf1 && card.lite[1].sf1);
  nfc_felica_free(&card);
}

int main(int argc,char **argv) {
  crypto_vectors();
  assert(argc==2);const char *path=argv[1];legacy_files(path);
  nfc_felica_card_t card={0},read={0};make_standard(&card);nfc_felica_listener_init(&remote,&card);
  nfc_felica_read_options_t options={.should_cancel=should_cancel};
  assert(nfc_felica_read(&read,&options)==ESP_OK);assert(read.system_count==2);
  assert(!memcmp(read.tag.idm,first_poll_idm,8));
  for(size_t i=0;i<2;i++){assert(read.systems[i].service_count==2);assert(read.systems[i].area_count==1);assert(read.systems[i].block_count==3);assert(read.systems[i].key_version==0x1234);}
  check_file(path,&read);nfc_felica_free(&read);
  malformed(&remote);
  remote.system=1;uint8_t select[]={1,0xff,0xff};nfc_felica_frame_t res=request(&remote,2,select,sizeof(select));assert(res.size==13 && res.data[11]==0x34 && res.data[12]==0x12);
  uint8_t write[6+32]={1,9,0,2,0x80,0};write[6]=0x80;write[7]=1;memset(write+8,0x61,30);
  /* All descriptors precede all data. */
  uint8_t multi[8+32]={1,9,0,2,0x80,0,0x80,1};memset(multi+8,0xa5,32);
  res=request(&remote,8,multi,sizeof(multi));assert(res.size==12 && res.data[10]==0);
  assert(card.systems[1].blocks[0].block.data[0]==0xa5 && card.systems[1].blocks[1].block.data[0]==0xa5);
  assert(card.systems[0].blocks[0].block.data[0]==1);
  multi[7]=9;memset(multi+8,0x22,32);res=request(&remote,8,multi,sizeof(multi));assert(res.data[11]==0xa8);assert(card.systems[1].blocks[0].block.data[0]==0xa5);
  cancelled=true;assert(nfc_felica_read(&read,&options)==ESP_ERR_INVALID_STATE);cancelled=false;
  corrupt=true;assert(nfc_felica_read(&read,&options)==ESP_ERR_INVALID_RESPONSE);assert(!read.systems);corrupt=false;
  nfc_felica_free(&card);
  make_lite(&card);nfc_felica_listener_init(&remote,&card);
  options.key=block(&card,0x87)->data;
  assert(nfc_felica_read(&read,&options)==ESP_OK);
  assert(read.is_internal_auth && read.is_external_auth && read.has_key && read.blocks_total==28);
  check_file(path,&read);nfc_felica_free(&read);
  uint8_t authenticated_write[8+32]={1,9,0,2,0x80,0x92,0x80,0x91};
  authenticated_write[8]=1;
  memcpy(authenticated_write+32,block(&card,0x90)->data,3);
  assert(nfc_felica_mac_write(&remote.crypto,authenticated_write+32,
                              authenticated_write+8,authenticated_write+24)==ESP_OK);
  res=request(&remote,8,authenticated_write,sizeof(authenticated_write));assert(!res.data[11]);
  uint8_t after_counter[3];memcpy(after_counter,block(&card,0x90)->data,3);
  res=request(&remote,8,authenticated_write,sizeof(authenticated_write));assert(res.data[11]==0xb2);
  assert(!memcmp(after_counter,block(&card,0x90)->data,3));
  nfc_felica_listener_reset(&remote);assert(!remote.has_rc && !remote.is_authenticated && !block(&card,0x92)->data[0]);
  uint8_t read_mac[]={1,11,0,1,0x80,0x91};res=request(&remote,6,read_mac,sizeof(read_mac));assert(res.data[11]==0xb2);
  uint8_t wrong[16]={0};options.key=wrong;assert(nfc_felica_read(&read,&options)==ESP_OK);assert(!read.has_key && !read.is_internal_auth);nfc_felica_free(&read);
  options.key=NULL;block(&card,0x88)->data[6]=1;nfc_felica_listener_init(&remote,&card);
  assert(nfc_felica_read(&read,&options)==ESP_OK);assert(read.lite[0].sf2==0xb1);check_file(path,&read);nfc_felica_free(&read);
  /* Saved unknown keys never become valid all-zero keys. */
  card.has_key=false;nfc_felica_listener_init(&remote,&card);
  uint8_t rc[6+16]={1,9,0,1,0x80,0x80};res=request(&remote,8,rc,sizeof(rc));assert(res.data[10]==0);assert(!remote.has_rc);
  res=request(&remote,6,read_mac,sizeof(read_mac));assert(res.data[11]==0xb2);
  assert(exchanges>50);
  puts("FeliCa: structured reads, Lite authentication, scoped files, listener validation and lifecycle OK");
  return 0;
}
