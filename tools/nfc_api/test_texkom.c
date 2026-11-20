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

#include "texkom.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void encode(const uint8_t frame[8], uint16_t *gaps, bool tk17) {
  if (tk17) {
    const unsigned shares[] = {20,60,40,80};
    for (unsigned byte=0;byte<8;byte++)
      for (unsigned pair=0;pair<4;pair++) {
        unsigned share=shares[(frame[byte]>>(pair*2))&3];
        gaps[byte*8+pair*2]=100-share; gaps[byte*8+pair*2+1]=share;
      }
  } else {
    for (unsigned bit=0;bit<64;bit++) {
      bool value=(frame[bit/8]>>(7-bit%8))&1;
      gaps[bit*2]=value?80:40;
      if (bit<63) gaps[bit*2+1]=value?40:80;
    }
  }
}
int main(void) {
  /* Independent fixed checksum vectors: UID 01 02 03 04. */
  uint8_t frames[][8]={{255,255,0x63,1,2,3,4,0xFE},
                         {255,255,0xCA,1,2,3,4,0xF4},
                         {255,255,255,255,1,2,3,0xF0}};
  uint16_t gaps[127];texkom_data_t data;
  for (unsigned i=0;i<3;i++) {
    encode(frames[i],gaps,i==1);
    size_t count=i==1?64:127;
    assert(texkom_decode(gaps,count,&data)==ESP_OK);
    assert(memcmp(data.frame,frames[i],8)==0);
    uint16_t cycles[128];size_t encoded_count=128;
    assert(texkom_encode(&data,cycles,&encoded_count)==ESP_OK);
    assert(encoded_count==(i==1?65:128));
    for(size_t gap=0;gap<count;gap++) gaps[gap]=cycles[gap]+64;
    assert(texkom_decode(gaps,count,&data)==ESP_OK);
    assert(memcmp(data.frame,frames[i],8)==0);
    for (unsigned bit=0;bit<64;bit++) {
      uint8_t changed[8];memcpy(changed,frames[i],8);changed[bit/8]^=1U<<(bit%8);
      if(i==2 && bit>=56 && bit<60) continue; /* MMBIT low nibble is payload. */
      encode(changed,gaps,i==1);
      assert(texkom_decode(gaps,count,&data)!=ESP_OK);
    }
  }
  memset(gaps,0,sizeof(gaps));
  assert(texkom_decode(gaps,127,&data)!=ESP_OK);
  assert(texkom_decode(gaps,64,&data)!=ESP_OK);
  assert(texkom_decode(NULL,64,&data)==ESP_ERR_INVALID_ARG);
  assert(texkom_decode(gaps,128,&data)==ESP_ERR_INVALID_ARG);
  unsigned random=1;
  for(unsigned round=0;round<10000;round++) {
    for(unsigned i=0;i<127;i++) { random=random*1664525U+1013904223U;gaps[i]=random>>16; }
    (void)texkom_decode(gaps,round%2?64:127,&data);
  }
  puts("PASS: Texkom fixed frames, checksum rejection, malformed pulses and bounds");
}
