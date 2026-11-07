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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM -1
#define HB_NFC_ERR_PROTOCOL -2
#define HB_NFC_ERR_AUTH -4
#define ESP_ERR_INVALID_STATE -3
#define ESP_LOGI(tag,...) ((void)(tag))
#define MFP_BLOCK_SIZE 16U
#define MFP_VERSION_SIZE 28U
#define MFP_SIGNATURE_SIZE 56U
typedef int hb_nfc_err_t;
typedef struct {uint8_t uid[10],uid_len,atqa[2],sak;} nfc_iso14443a_data_t;
typedef struct {uint8_t ats[64];size_t ats_len;} nfc_iso_dep_data_t;
typedef struct {nfc_iso_dep_data_t dep;bool authenticated;uint16_t key;} mfp_session_t;
typedef struct {
 struct {uint8_t uid[7],uid_len,atqa[2],sak,ats[32],ats_len;} identity;
 uint16_t block_count;char card_type[32];uint8_t version[28],signature[56];bool has_signature;
 uint8_t blocks[256][16],keys[80][16],extra[8][16];
 bool has_block[256],has_key[80],has_extra[8],extra_present[8];
} nfc_plus_file_t;
typedef bool (*nfc_plus_poller_cancel_cb_t)(void);
static unsigned geometry=128,writes,reads,auths,fail_write;
static bool stop,plain_only,wrong_family,no_version,sl0,link_failed;
static hb_nfc_err_t mfp_poller_init(const nfc_iso14443a_data_t *id,mfp_session_t *s) {
 (void)id;memset(s,0,sizeof(*s));
 const uint8_t ats[]={9,0,0xC1,5,0x21,0x30,0,0,0};memcpy(s->dep.ats,ats,9);s->dep.ats_len=wrong_family?0:9;
 return 0;
}
static hb_nfc_err_t mfp_get_version(mfp_session_t *s,uint8_t *out) {
 (void)s;if(no_version)return -2;memset(out,0,28);out[0]=4;out[1]=wrong_family?1:2;
 out[3]=0x11;out[5]=geometry==128?0x16:0x18;return 0;
}
static int mfp_apdu_transceive(mfp_session_t *s,const uint8_t *tx,size_t n,uint8_t *rx,size_t cap,int timeout) {
 (void)s;(void)timeout;assert(cap>=1 && n==19 && tx[0]==0xA8 && tx[1]==0xFF && tx[2]==0xFF);
 rx[0]=wrong_family?0:sl0?9:0x0B;return 1;
}
static hb_nfc_err_t mfp_read_signature(mfp_session_t *s,uint8_t *out) {(void)s;memset(out,0x56,56);return 0;}
static hb_nfc_err_t mfp_sl3_auth_first(mfp_session_t *s,uint16_t address,const uint8_t *key) {
 auths++;s->authenticated=false;
 if(link_failed)return HB_NFC_ERR_PROTOCOL;
 if(key[0]!=0xA5)return HB_NFC_ERR_AUTH;
 s->key=address;s->authenticated=true;return 0;
}
static hb_nfc_err_t mfp_sl3_read(mfp_session_t *s,uint16_t address,uint8_t count,bool encrypted,uint8_t *out,size_t cap,size_t *len) {
 assert(s->authenticated && count==1 && cap>=16);reads++;
 if(plain_only && encrypted) {s->authenticated=false;return -2;}
 memset(out,(uint8_t)address,16);*len=16;return 0;
}
static hb_nfc_err_t mfp_sl3_write(mfp_session_t *s,uint16_t address,bool encrypted,const uint8_t *data,size_t n) {
 assert(s->authenticated && encrypted && n==16 && data[0]==(uint8_t)address);
 assert(address!=0 && (address<128?address%4!=3:address%16!=15));
 writes++;return fail_write && writes==fail_write ? -2:0;
}
/* PRODUCTION */
static bool should_cancel(void) {return stop;}
int main(void) {
 nfc_iso14443a_data_t id={.uid={1,2,3,4},.uid_len=4,.atqa={4,0},.sak=0x20};
 uint8_t level;
 uint8_t keys[2][16]={{0},{0xA5}};nfc_plus_file_t image;
 assert(nfc_plus_poller_read(&id,keys[0],2,should_cancel,&image,&level)==0);
 assert(image.block_count==128 && image.has_signature && !strcmp(image.card_type,"Mifare Plus EV1"));
 for(unsigned i=0;i<128;i++)assert(image.has_block[i] && image.blocks[i][0]==(uint8_t)i);
 for(unsigned i=0;i<64;i++)assert(image.has_key[i]);
 for(unsigned i=0;i<8;i++)assert(image.has_extra[i] && image.extra_present[i]);
 uint16_t written=999;writes=0;
 assert(nfc_plus_poller_write(&id,&image,true,should_cancel,&written)==0 && written==95 && writes==95);
 id.uid[0]^=1;writes=0;
 assert(nfc_plus_poller_write(&id,&image,true,should_cancel,&written)!=0 && writes==0 && written==0);
 id.uid[0]^=1;fail_write=2;
 assert(nfc_plus_poller_write(&id,&image,true,should_cancel,&written)!=0 && writes==2 && written==1);
 fail_write=0;stop=true;writes=0;
 assert(nfc_plus_poller_write(&id,&image,true,should_cancel,&written)!=0 && writes==0);
 assert(nfc_plus_poller_read(&id,keys[0],2,should_cancel,&image,&level)!=0);
 stop=false;plain_only=true;geometry=256;
 assert(nfc_plus_poller_read(&id,keys[0],2,should_cancel,&image,&level)==0 && image.block_count==256 && image.has_block[255]);
 assert(nfc_plus_poller_write(&id,&image,true,should_cancel,&written)==0 && written==215);
 no_version=true;geometry=64;
 assert(nfc_plus_poller_read(&id,keys[0],2,should_cancel,&image,&level)==0 && image.block_count==64 && !strcmp(image.card_type,"Mifare Plus SE"));
 assert(nfc_plus_poller_read(&id,keys[0],1,should_cancel,&image,&level)==0 && !image.has_block[1] && !image.has_key[0]);
 link_failed=true;unsigned auth_before=auths;
 assert(nfc_plus_poller_read(&id,keys[0],2,should_cancel,&image,&level)==HB_NFC_ERR_PROTOCOL && auths==auth_before+1);
 link_failed=false;
 wrong_family=true;
 assert(nfc_plus_poller_read(&id,keys[0],2,should_cancel,&image,&level)!=0);
 wrong_family=false;no_version=false;sl0=true;
 unsigned before_auth=auths;
 assert(nfc_plus_poller_read(&id,keys[0],2,should_cancel,&image,&level)==0 && level==0 && auths==before_auth);
 sl0=false;id.sak=0x10;
 assert(nfc_plus_poller_read(&id,keys[0],2,should_cancel,&image,&level)==0 && level==2 && auths==before_auth);
 id.sak=0x08;
 assert(nfc_plus_poller_read(&id,keys[0],2,should_cancel,&image,&level)==0 && level==1 && auths==before_auth);
 id.uid_len=255;writes=0;
 assert(nfc_plus_poller_write(&id,&image,true,should_cancel,&written)!=0 && writes==0);
 puts("Plus workflow: complete/partial reads, admin/config, all geometries, modes, cancellation and guarded writes OK");
}
