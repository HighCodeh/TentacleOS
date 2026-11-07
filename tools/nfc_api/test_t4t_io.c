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

#include "t4t.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t cc_data[]={0,18,0x20,0,15,0,15,5,1,0,4,6,0xE1,4,0,18,0,0};
static uint8_t file[18];
static unsigned updates;
static bool selected_cc, fail_payload;
int iso_dep_apdu_transceive(const nfc_iso_dep_data_t *dep,const uint8_t *apdu,size_t length,
    uint8_t *out,size_t cap,int timeout) {
  (void)dep;(void)length;(void)timeout;assert(cap>=2);
  size_t size=0, offset=(apdu[2]<<8)|apdu[3];
  if(apdu[1]==0xA4) {if(apdu[2]==0)selected_cc=apdu[6]==3;}
  else if(apdu[1]==0xB0) {
    size=apdu[4]; assert(size+2<=cap && offset+size<=18);
    memcpy(out,(selected_cc?cc_data:file)+offset,size);
  } else if(apdu[1]==0xD6) {
    assert(!selected_cc && offset+apdu[4]<=18);
    if(fail_payload && offset) return -1;
    updates++; memcpy(file+offset,apdu+5,apdu[4]);
  } else assert(0);
  out[size]=0x90;out[size+1]=0;return size+2;
}
int main(void) {
  nfc_iso_dep_data_t dep={0};t4t_cc_t cc;
  assert(t4t_read_cc(&dep,&cc)==0 && cc.ndef_max==18 && cc.ndef_fid==0xE104);
  uint8_t payload[17]={1,2,3},actual[18];size_t length=99;
  assert(t4t_write_ndef(&dep,&cc,payload,17)!=0 && !updates);
  assert(t4t_write_ndef(&dep,&cc,payload,16)==0 && updates==4 && file[1]==16);
  assert(t4t_read_ndef(&dep,&cc,actual,sizeof(actual),&length)==0 && length==16 && !memcmp(actual,payload,16));
  assert(t4t_update_binary_ndef(&dep,&cc,18,payload,1)!=0);
  assert(t4t_read_binary_ndef(&dep,&cc,18,actual,1)!=0);
  fail_payload=true;
  assert(t4t_write_ndef(&dep,&cc,payload,16)!=0 && file[0]==0 && file[1]==0);
  cc_data[11]=9;
  assert(t4t_read_cc(&dep,&cc)!=0);
  puts("Type 4 CC TLVs, NLEN bounds, chunks and interrupted write tests passed");
}
