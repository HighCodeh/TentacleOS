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
#include <stdio.h>
#include <string.h>
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM -1
#define HB_NFC_ERR_TIMEOUT -2
#define HB_NFC_ERR_PROTOCOL -3
#define ISO15693_UID_LEN 8
#define ISO15693_FLAGS_ADDRESSED 0x22
#define SLIX_SIGNATURE_BYTES 32
#define SLIX_PASSWORD_BYTES 4
#define ESP_LOGD(...) ((void)0)
typedef int hb_nfc_err_t;
typedef struct {uint8_t uid[8];} iso15693_tag_t;
typedef enum { SLIX_PASSWORD_READ,SLIX_PASSWORD_WRITE,SLIX_PASSWORD_PRIVACY,
 SLIX_PASSWORD_DESTROY,SLIX_PASSWORD_EAS_AFI,SLIX_PASSWORD_COUNT } slix_password_t;
typedef struct {uint8_t protection_pointer,protection_condition,lock_bits;
 uint32_t features;} slix_system_info_t;
static int calls, fail_call;
static bool crc_valid=true, short_response, error_flag;
static uint8_t password_type;
static bool iso15693_check_crc(const uint8_t *p,size_t n) {(void)p;(void)n;return crc_valid;}
static int nfc_poller_transceive(const uint8_t *tx,size_t size,bool crc,uint8_t *rx,
 size_t max,size_t min,uint32_t timeout) {
 (void)min;(void)timeout;assert(crc && tx[2]==4);calls++;
 if(calls==fail_call) return 0;
 memset(rx,0,max);rx[0]=error_flag?1:0;
 size_t len=0;
 switch(tx[1]) {
 case 0xB2: assert(tx[0]==2 && size==3);rx[1]=0x12;rx[2]=0x34;len=5;break;
 case 0xB3: {
  size_t offset=password_type==2?3:11;
  assert(tx[0]==(password_type==2?2:0x22) && size==offset+5);
  assert(tx[offset]==(1U<<password_type));
  const uint8_t masked[]={0x13,0x17,0x57,0x53};
  assert(memcmp(tx+offset+1,masked,4)==0);len=3;break;
 }
 case 0xAB: assert(tx[0]==0x22 && size==11);len=10;
  rx[1]=5;rx[2]=6;rx[3]=7;rx[4]=0x78;rx[5]=0x56;rx[6]=0x34;rx[7]=0x12;break;
 case 0xBD: assert(tx[0]==0x22 && size==11);len=35;
  for(size_t i=1;i<=32;i++) rx[i]=(uint8_t)i;break;
 default:assert(false);
 }
 return (int)len-(short_response?1:0);
}
/* PRODUCTION */
int main(void) {
 iso15693_tag_t tag={{0,1,2,3,4,5,6,7}};
 const uint8_t password[]={1,0x23,0x45,0x67};
 for(password_type=0;password_type<SLIX_PASSWORD_COUNT;password_type++) {
  calls=0;assert(slix_authenticate(&tag,password_type,password)==0 && calls==2);
 }
 password_type=2;
 for(fail_call=1;fail_call<=2;fail_call++) {
  calls=0;assert(slix_authenticate(NULL,SLIX_PASSWORD_PRIVACY,password)==HB_NFC_ERR_TIMEOUT);
  assert(calls==fail_call);
 }
 fail_call=0;calls=0;short_response=true;
 assert(slix_authenticate(NULL,SLIX_PASSWORD_PRIVACY,password)==HB_NFC_ERR_PROTOCOL && calls==1);
 short_response=false;crc_valid=false;calls=0;
 assert(slix_authenticate(NULL,SLIX_PASSWORD_PRIVACY,password)==HB_NFC_ERR_PROTOCOL && calls==1);
 crc_valid=true;error_flag=true;calls=0;
 assert(slix_authenticate(NULL,SLIX_PASSWORD_PRIVACY,password)==HB_NFC_ERR_PROTOCOL && calls==1);
 error_flag=false;slix_system_info_t info;
 assert(slix_get_system_info(&tag,&info)==0 && info.features==0x12345678);
 assert(info.protection_pointer==5 && info.protection_condition==6 && info.lock_bits==7);
 uint8_t sig[32];assert(slix_read_signature(&tag,sig)==0 && sig[0]==1 && sig[31]==32);
 assert(slix_authenticate(NULL,SLIX_PASSWORD_READ,password)==HB_NFC_ERR_PARAM);
 assert(slix_authenticate(&tag,SLIX_PASSWORD_COUNT,password)==HB_NFC_ERR_PARAM);
 assert(slix_read_signature(NULL,sig)==HB_NFC_ERR_PARAM);
 puts("PASS: SLIX wire addressing, password XOR order, errors, system info and signature");
}
