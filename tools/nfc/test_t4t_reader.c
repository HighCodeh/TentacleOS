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

static uint8_t card_cc[300], card_file[0x10040];
static size_t cc_size, file_size;
static int selected, updates, extra, legacy_only, fail_update, fail_read, reads, odo_reads, odo_writes;
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void reset_card(void) {
  const uint8_t cc[]={0,15,0x20,0,15,0,16,4,6,0xE1,4,0,34,0,0};
  memset(card_cc,0,sizeof card_cc);memcpy(card_cc,cc,sizeof cc);
  cc_size=15;file_size=34;card_file[0]=0;card_file[1]=32;
  for(size_t i=2;i<sizeof card_file;i++)card_file[i]=(uint8_t)i;
  selected=updates=extra=legacy_only=fail_update=fail_read=reads=odo_reads=odo_writes=0;
}
static int iso_dep_apdu_transceive(const nfc_iso_dep_data_t*dep,const uint8_t*t,size_t n,uint8_t*r,size_t cap,int ms) {
  (void)dep;(void)ms;CHECK(n>=5&&t[0]==0);CHECK(cap>=2);size_t output=0;uint16_t sw=0x9000;
  if(t[1]==0xA4) {
    if(t[2]==4) {
      CHECK(n==13||n==12);CHECK(t[4]==7);
      if(t[11]==1&&legacy_only)sw=0x6A82;
      else CHECK(t[11]==(legacy_only?0:1));
      CHECK(n==(legacy_only&&t[11]==0?12:13));selected=0;
    } else {
      CHECK(n==7&&t[4]==2&&t[5]==0xE1&&(t[6]==3||t[6]==4));
      CHECK(t[3]==(legacy_only?0:12));selected=t[6];
    }
  } else if(t[1]==0xB0||t[1]==0xB1) {
    size_t offset;
    if(t[1]==0xB0) {CHECK(n==5&&t[2]<0x80);offset=(t[2]<<8)|t[3];output=t[4];}
    else {CHECK(n==11&&t[2]==0&&t[3]==0&&t[4]==5&&t[5]==0x54&&t[6]==3);
      offset=((size_t)t[7]<<16)|(t[8]<<8)|t[9];
      output=t[10]-(t[10]>129?3:2);odo_reads++;
    }
    uint8_t*file=selected==3?card_cc:card_file;size_t size=selected==3?cc_size:file_size;
    CHECK(offset+output<=size);CHECK(output+5+extra<=cap);CHECK((selected==3&&offset==0&&output==7)||output<=(size_t)((card_cc[3]<<8)|card_cc[4]));
    if(++reads==fail_read)return 0;
    size_t header=0;
    if(t[1]==0xB1){r[header++]=0x53;if(output>127)r[header++]=0x81;r[header++]=output;}
    memcpy(r+header,file+offset,output);output+=header+extra;
  } else if(t[1]==0xD6||t[1]==0xD7) {
    CHECK(selected==4&&n==(size_t)t[4]+5U);size_t offset, count, start;
    if(t[1]==0xD6){CHECK(t[2]<0x80);offset=(t[2]<<8)|t[3];count=t[4];start=5;}
    else {CHECK(t[2]==0&&t[3]==0&&t[5]==0x54&&t[6]==3&&t[10]==0x53);
      offset=((size_t)t[7]<<16)|(t[8]<<8)|t[9];start=t[11]==0x81?13:12;count=t[start-1];odo_writes++;
    }
    CHECK(start+count==n&&offset+count<=file_size&&t[4]<=((card_cc[5]<<8)|card_cc[6]));
    updates++;if(updates==fail_update)sw=0x6581;else memcpy(card_file+offset,t+start,count);
  } else CHECK(false);
  r[output++]=sw>>8;r[output++]=sw;return (int)output;
}
int main(void) {
  nfc_iso_dep_data_t dep={0};t4t_cc_t cc={0};uint8_t out[40];size_t length=99;
  reset_card();CHECK(t4t_read_cc(&dep,&cc)==0);CHECK(cc.ndef_max==34);
  memset(out,0xA5,sizeof out);CHECK(t4t_read_ndef(&dep,&cc,out+1,32,&length)==0&&length==32);
  CHECK(out[0]==0xA5&&out[33]==0xA5&&!memcmp(out+1,card_file+2,32));
  CHECK(t4t_write_ndef(&dep,&cc,out+1,32)==0&&updates==4);
  updates=0;CHECK(t4t_write_ndef(&dep,&cc,out,33)!=0&&updates==0);
  card_file[1]=33;length=99;CHECK(t4t_read_ndef(&dep,&cc,out,sizeof out,&length)!=0&&length==0);
  CHECK(t4t_write_ndef(&dep,&cc,NULL,0)==0);
  CHECK(t4t_read_ndef(&dep,&cc,out,sizeof out,&length)==0&&length==0);
  CHECK(t4t_read_binary_ndef(&dep,&cc,33,out,2)!=0);
  CHECK(t4t_update_binary_ndef(&dep,&cc,33,out,2)!=0);
  CHECK(t4t_read_binary_ndef(&dep,&cc,65535,out,(size_t)-1)!=0);
  CHECK(t4t_update_binary_ndef(&dep,&cc,65535,out,(size_t)-1)!=0);
  CHECK(t4t_read_binary_ndef(&dep,&cc,33,out,1)==0);
  CHECK(t4t_update_binary_ndef(&dep,&cc,33,out,1)==0);
  reset_card();card_cc[2]=0x10;legacy_only=1;
  CHECK(t4t_read_cc(&dep,&cc)==0&&cc.legacy);CHECK(t4t_read_ndef(&dep,&cc,out,sizeof out,&length)==0);
  // An extra proprietary TLV before NDEF, followed by another file control TLV.
  reset_card();memmove(card_cc+12,card_cc+7,8);memcpy(card_cc+7,"\x05\x03\x01\x02\x03",5);
  memcpy(card_cc+20,card_cc+12,8);card_cc[1]=cc_size=28;
  CHECK(t4t_read_cc(&dep,&cc)==0&&cc.ndef_fid==0xE104);
  reset_card();memmove(card_cc+11,card_cc+9,6);card_cc[8]=0xFF;card_cc[9]=0;card_cc[10]=6;
  card_cc[1]=cc_size=17;CHECK(t4t_read_cc(&dep,&cc)==0&&cc.ndef_fid==0xE104);
  reset_card();memmove(card_cc+8,card_cc+7,8);card_cc[7]=0;card_cc[1]=cc_size=16;
  CHECK(t4t_read_cc(&dep,&cc)==0&&cc.nlen_size==2);
  const unsigned invalid[]={1,2,3,5,7,8,11,13,14};
  for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++) {
    reset_card();unsigned at=invalid[i];
    if(at==1)card_cc[1]=14;
    if(at==2)card_cc[2]=0x40;
    if(at==3)card_cc[3]=card_cc[4]=0;
    if(at==5)card_cc[5]=card_cc[6]=0;
    if(at==7)card_cc[7]=6;
    if(at==8)card_cc[8]=7;
    if(at==11)card_cc[11]=0x81;
    if(at==13||at==14)card_cc[at]=1;
    CHECK(t4t_read_cc(&dep,&cc)!=0&&cc.ndef_max==0);
  }
  reset_card();extra=1;CHECK(t4t_read_cc(&dep,&cc)!=0);
  reset_card();CHECK(t4t_read_cc(&dep,&cc)==0);fail_update=3;
  CHECK(t4t_write_ndef(&dep,&cc,out,32)!=0&&updates==3&&card_file[0]==0&&card_file[1]==0);
  reset_card();CHECK(t4t_read_cc(&dep,&cc)==0);fail_read=reads+3;length=99;
  CHECK(t4t_read_ndef(&dep,&cc,out,sizeof out,&length)!=0&&length==0);
  // Mapping v3, four-byte ENLEN, crossing 0x8000 and 0x10000 with ODO/DDO.
  reset_card();const uint8_t extended[]={0,17,0x30,0,255,0,255,6,8,0xE1,4,0,1,0,32,0,0};
  memcpy(card_cc,extended,sizeof extended);cc_size=17;file_size=0x10020;
  card_file[0]=0;card_file[1]=1;card_file[2]=0;card_file[3]=16;
  CHECK(t4t_read_cc(&dep,&cc)==0&&cc.ndef_max==0x10020&&cc.nlen_size==4);
  uint8_t *large=malloc(0x10020);CHECK(large!=NULL);
  CHECK(t4t_read_ndef(&dep,&cc,large,0x10020,&length)==0&&length==0x10010&&odo_reads>0);
  CHECK(!memcmp(large,card_file+4,length));
  CHECK(t4t_write_ndef(&dep,&cc,large,length)==0&&odo_writes>0);
  CHECK(get_be(card_file,4)==length&&!memcmp(large,card_file+4,length));
  CHECK(t4t_read_binary_ndef(&dep,&cc,0x10000,out,16)==0);
  CHECK(t4t_update_binary_ndef(&dep,&cc,0x10000,out,16)==0);
  extra=1;CHECK(t4t_read_binary_ndef(&dep,&cc,0x10000,out,16)!=0);extra=0;
  cc.write_access=0xFF;updates=0;CHECK(t4t_write_ndef(&dep,&cc,large,16)!=0&&updates==0);
  CHECK(t4t_read_ndef(&dep,&cc,large,0x10020,&length)==0);
  free(large);
  printf("PASS: %u assertions; Type 4 v1/v2/v3, variable CC, 65552-byte NDEF, ODO/DDO, bounds, short responses and interrupted writes\n",checks);
}
