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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nfc_plus_file.h"
int storage_write_atomic(const char *path,const void *data,size_t length) {
 if(strstr(path,"fail"))return ESP_FAIL;
 FILE *f=fopen(path,"wb");if(!f)return ESP_FAIL;
 bool ok=fwrite(data,1,length,f)==length;
 return fclose(f)==0 && ok ? ESP_OK : ESP_FAIL;
}
static void make_file(const char *path,int variant) {
 FILE *f=fopen(path,"w");assert(f);
 fprintf(f,"Filetype: Flipper NFC device\nVersion: 4\nDevice type: Mifare Plus\nUID: 01 02 03 04\nATQA: 00 04\nSAK: 20\nSecurity Level: SL3\nMemory Size: 1K\nData format version: 1\nCard Type: Mifare Plus EV1\nPICC Version:");
 for(unsigned i=0;i<28;i++)fprintf(f," %02X",i);fputc('\n',f);
 for(unsigned i=0;i<64;i++) {
   if(variant==1 && i==63)continue;
   fprintf(f,"Block %u:",i);
   for(unsigned j=0;j<16;j++) {
     if(i==8)fprintf(f," ??");
     else if(i%4==3 && j==6)fprintf(f," FF");
     else if(i%4==3 && j==7)fprintf(f," 07");
     else if(i%4==3 && j==8)fprintf(f," 80");
     else fprintf(f," 11");
   }
   fputc('\n',f);
 }
 for(unsigned i=0;i<16;i++)for(unsigned k=0;k<2;k++) {
   fprintf(f,"Key %c %u:",k?'B':'A',i);
   for(unsigned j=0;j<16;j++)fprintf(f,i==0 && !k ? " 22" : " ??");
   fputc('\n',f);
 }
 if(variant==2)fprintf(f,"UID: 01 02 03 04\n");
 if(variant==3)fprintf(f,"Block 64: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n");
 if(variant==4)fprintf(f,"Signature: ??\n");
 if(variant==5) {for(unsigned i=0;i<300;i++)fputc('a',f);fputc('\n',f);}
 fclose(f);
}
int main(int argc,char **argv) {
 assert(argc==2);nfc_plus_file_t card;
 make_file(argv[1],0);assert(nfc_plus_file_load(argv[1],&card)==0);
 assert(card.block_count==64 && card.identity.uid_len==4 && card.identity.atqa[0]==4);
 const uint8_t ats[]={3,0x46,2};
 assert(card.identity.ats_len==sizeof(ats) && memcmp(card.identity.ats,ats,sizeof(ats))==0);
 assert(card.has_block[0] && !card.has_block[8] && card.has_key[0] && !card.has_key[1]);
 mfp_listener_config_t config;nfc_plus_file_listener(&card,&config);
 assert(config.write==NULL && config.signature==NULL);
 uint8_t block[16];
 assert(config.key(config.context,0x4000,block) && block[0]==0x22);
 assert(!config.key(config.context,0x4001,block));
 assert(config.read(config.context,0x4000,0,block) && block[0]==0x11);
 assert(!config.read(config.context,0x4000,4,block));
 assert(!config.read(config.context,0x4004,8,block));
 nfc_plus_file_listener_mutable(&card,&config);
 assert(config.write!=NULL);
 memset(block,0xA5,sizeof(block));
 assert(config.write(config.context,0x4000,1,block));
 assert(!config.write(config.context,0x4000,0,block));
 uint8_t invalid_trailer[16]={0};
 assert(!config.write(config.context,0x4000,3,invalid_trailer));
 assert(!config.write(config.context,0x4000,4,block));
 assert(!config.write(config.context,0x4001,1,block));
 assert(config.read(config.context,0x4000,1,block) && block[0]==0xA5);
 card.blocks[3][6]^=1;assert(!config.write(config.context,0x4000,1,block));
 card.blocks[3][6]^=1;
 assert(config.read(config.context,0x4000,3,block));
 for(unsigned i=0;i<6;i++)assert(block[i]==0 && block[i+10]==0);
 card.extra_present[0]=true;card.has_extra[0]=true;memset(card.extra[0],0xB6,16);
 card.extra_present[7]=true;
 nfc_plus_file_t loaded;
 for(unsigned geometry=64;geometry<=256;geometry*=2) {
   card.block_count=geometry;
   assert(nfc_plus_file_save(argv[1],&card)==0);
   assert(nfc_plus_file_load(argv[1],&loaded)==0);
   assert(loaded.block_count==geometry && loaded.blocks[1][0]==0xA5);
   assert(!loaded.has_block[8] && !loaded.has_key[1]);
   assert(loaded.extra_present[0] && loaded.has_extra[0] && loaded.extra[0][15]==0xB6);
   assert(loaded.extra_present[7] && !loaded.has_extra[7]);
   assert(!strcmp(loaded.card_type,"Mifare Plus EV1"));
 }
 card.has_key[1]=true;
 const unsigned rm[]={3,3,3,2,3,2,3,0},wm[]={3,0,0,2,2,0,2,0};
 for(unsigned condition=0;condition<8;condition++) {
   unsigned c1=(condition>>2)&1,c2=(condition>>1)&1,c3=condition&1;
   card.blocks[3][6]=((~c2&15)<<4)|(~c1&15);
   card.blocks[3][7]=(c1<<4)|(~c3&15);card.blocks[3][8]=(c3<<4)|c2;
   for(unsigned key=0;key<2;key++) {
     assert(config.read(config.context,0x4000+key,0,block)==((rm[condition]&(1U<<key))!=0));
   }
   /* Apply the same condition to group 1 to exercise writable data. */
   c1<<=1;c2<<=1;c3<<=1;
   card.blocks[3][6]=((~c2&15)<<4)|(~c1&15);
   card.blocks[3][7]=(c1<<4)|(~c3&15);card.blocks[3][8]=(c3<<4)|c2;
   for(unsigned key=0;key<2;key++)
     assert(config.write(config.context,0x4000+key,1,block)==((wm[condition]&(1U<<key))!=0));
 }
 card.has_key[64]=true;card.has_block[143]=true;
 card.blocks[143][6]=0xFF;card.blocks[143][7]=0x0F;card.blocks[143][8]=0;
 for(unsigned address=128;address<143;address++)assert(config.write(config.context,0x4040,address,block));
 assert(!config.write(config.context,0x4040,143,block));
 assert(!config.write(config.context,0x4040,144,block));
 /* Lock group 1 (blocks 133..137), leaving both adjacent groups writable. */
 card.blocks[143][6]=0xDD;card.blocks[143][7]=0x2D;card.blocks[143][8]=0x22;
 for(unsigned address=128;address<143;address++)
   assert(config.write(config.context,0x4040,address,block)==(address<133 || address>137));
 /* Exhaust all trailer policies for both AES keys, independently of data ACLs. */
 const unsigned tr[]={1,1,1,3,3,3,3,3};
 const unsigned tw[]={0,1,0,2,0,2,0,0};
 const unsigned kw[]={1,1,0,2,2,0,0,0};
 for(unsigned condition=0;condition<8;condition++) {
   unsigned c1=((condition>>2)&1)<<3,c2=((condition>>1)&1)<<3,c3=(condition&1)<<3;
   uint8_t trailer[16]={0};
   trailer[6]=((~c2&15)<<4)|(~c1&15);
   trailer[7]=(c1<<4)|(~c3&15);trailer[8]=(c3<<4)|c2;
   trailer[9]=0x69;
   memcpy(card.blocks[3],trailer,16);
   for(unsigned key=0;key<2;key++) {
     assert(config.read(config.context,0x4000+key,3,block)==((tr[condition]&(1U<<key))!=0));
     assert(config.write(config.context,0x4000+key,3,trailer)==((tw[condition]&(1U<<key))!=0));
     for(unsigned target=0;target<2;target++) {
       memset(block,0x30+target,16);
       assert(config.write(config.context,0x4000+key,0x4000+target,block)==((kw[condition]&(1U<<key))!=0));
     }
     assert(!config.write(config.context,0x4000+key,0x4002,block));
   }
 }
 card.blocks[3][6]=0xFF;card.blocks[3][7]=0x07;card.blocks[3][8]=0x80;
 memcpy(block,card.blocks[3],16);block[9]=0x42;
 assert(config.write(config.context,0x4000,3,block));
 assert(card.blocks[3][9]==0x42);
 block[6]^=1;assert(!config.write(config.context,0x4000,3,block));
 assert(card.blocks[3][6]==0xFF);
 memset(block,0xA7,16);
 assert(config.write(config.context,0x4000,0x4001,block));
 assert(config.key(config.context,0x4001,block) && block[0]==0xA7);
 assert(nfc_plus_file_save(argv[1],&card)==0);
 assert(nfc_plus_file_load(argv[1],&loaded)==0 && loaded.keys[1][15]==0xA7);
 card.block_count=64;card.has_key[64]=true;
 assert(!config.key(config.context,0x4040,block));
 assert(!config.write(config.context,0x4000,0x4040,block));
 assert(config.write(config.context,0x4000,0x9000,block));
 assert(config.key(config.context,0x9000,block) && block[0]==0xA7);
 assert(!config.key(config.context,0x9002,block));
 assert(config.write(config.context,0x9000,0xB000,block));
 assert(config.read(config.context,0x9000,0xB000,block));
 assert(!config.read(config.context,0x9000,0xB004,block));
 assert(!config.write(config.context,0x9002,0xB000,block));
 assert(nfc_plus_file_save(argv[1],&card)==0);
 assert(nfc_plus_file_load(argv[1],&loaded)==0 && loaded.extra[4][15]==0xA7);
 assert(nfc_plus_file_save("failure",&card)==ESP_FAIL);
 card.block_count=65;assert(nfc_plus_file_save(argv[1],&card)==ESP_ERR_INVALID_ARG);
 for(int i=1;i<=5;i++) {
  make_file(argv[1],i);assert(nfc_plus_file_load(argv[1],&card)!=0);
  assert(card.block_count==0 && !card.has_key[0]);
 }
 puts("Plus file: strict parsing, access checks, RAM writes, all geometries and export round-trip OK");
}
