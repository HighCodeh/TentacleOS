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

static nfc_iso_dep_data_t dep;
static uint8_t reply[64][270], wanted[64][270];
static int reply_len[64], wanted_len[64], steps, cursor, calls;
static int64_t clock_us;
static uint8_t received[2048];
static unsigned checks;
static bool corrupt_crc;
#define CHECK(x) do { checks++; if (!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1);} } while(0)
static unsigned hex(const char *s,uint8_t *out) {
 unsigned n=0,v; while(*s){ while(*s==' ')s++; if(!*s)break; CHECK(sscanf(s,"%2x",&v)==1);out[n++]=v;s+=2;} return n;
}
static void add(const char*t,const char*r) {wanted_len[steps]=hex(t,wanted[steps]);reply_len[steps]=hex(r,reply[steps]);steps++;}
static void clear(void) {steps=cursor=calls=0;memset(received,0xA5,sizeof received);}
static void setup(uint8_t fsci,uint8_t cid) {
 clear(); uint8_t ats[]={5,(uint8_t)(0x60|fsci),0x40,2,0};
 wanted[0][0]=0xE0;wanted[0][1]=0x80|cid;wanted_len[0]=2;
 memcpy(reply[0],ats,5);reply_len[0]=5;steps=1;
 CHECK(iso_dep_rats(8,cid,&dep)==0);CHECK(cursor==steps);clear();
}
static int64_t esp_timer_get_time(void){return clock_us;}
static void hb_nfc_timer_delay_ms(uint32_t ms){clock_us+=(int64_t)ms*1000;}
static bool iso14443a_check_crc(const uint8_t*d,size_t n){
 if(n<3)return false; uint16_t crc=0x6363;
 for(size_t i=0;i<n-2;i++){crc^=d[i];for(int j=0;j<8;j++)crc=(crc>>1)^((crc&1)?0x8408:0);}
 return d[n-2]==(uint8_t)crc&&d[n-1]==(uint8_t)(crc>>8);
}
static int nfc_poller_transceive(const uint8_t*t,size_t n,bool crc,uint8_t*r,size_t cap,size_t min,int ms){
 CHECK(crc&&min==1&&ms>0&&ms<=5000);CHECK(++calls<200);clock_us+=100;
 if(cursor>=steps)return 0;
 int k=cursor++;CHECK(n==(size_t)wanted_len[k]);CHECK(!memcmp(t,wanted[k],n));
 if (!reply_len[k]) return 0;
 CHECK((size_t)reply_len[k]+2<=cap);memcpy(r,reply[k],reply_len[k]);
 uint16_t checksum=0x6363;
 for(int i=0;i<reply_len[k];i++){checksum^=r[i];for(int j=0;j<8;j++)checksum=(checksum>>1)^((checksum&1)?0x8408:0);}
 r[reply_len[k]]=(uint8_t)checksum;r[reply_len[k]+1]=(uint8_t)(checksum>>8);
 if(corrupt_crc){r[reply_len[k]]^=1;corrupt_crc=false;}
 return reply_len[k]+2;
}
int main(void){
 setup(8,0);CHECK(dep.fsc==256);CHECK(dep.fsd==256);
 uint8_t command[]={0x00,0xB0,0,0,1};
 add("0200B0000001","029000");CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==2);
 add("0300B0000001","039000");CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==2);
 setup(8,0);add("0200B0000001","12AABB");add("A3","03CC9000");
 CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==5);CHECK(!memcmp(received,"\xAA\xBB\xCC\x90\0",5));
 /* Duplicate chained response must be acknowledged without duplicating data. */
 setup(8,0);add("0200B0000001","12AA");add("A3","12AA");add("A3","039000");
 CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==3);CHECK(received[1]==0x90);
 setup(8,0);add("0200B0000001","12AA");
 CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==0);CHECK(calls<=4);
 setup(8,0);add("0200B0000001","02AABB9000");
 CHECK(iso_dep_transceive(&dep,command,5,received,3,0)==0);CHECK(received[3]==0xA5);
 setup(8,0);add("0200B0000001","039000");CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==0);
 /* FSC=16 leaves 13 bytes of INF, not 15. ACK carries the transmitted BN. */
 setup(0,0);uint8_t long_command[14]={0};
 add("1200000000000000000000000000","A2");add("0300","039000");
 CHECK(iso_dep_transceive(&dep,long_command,14,received,20,0)==2);
 setup(8,0);add("0200B0000001","F202");add("F202","029000");
 CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==2);
 /* Deselect must not be interpreted as WTX. */
 setup(8,0);add("0200B0000001","C2");CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==0);CHECK(calls==1);
 for(unsigned value=0;value<=63;value++){
  setup(8,0);add("0200B0000001","F200");reply[0][1]=value;
  if(value>=1&&value<=59){uint8_t temp[3]={0xF2,value,0};wanted_len[1]=2;memcpy(wanted[1],temp,2);reply_len[1]=3;memcpy(reply[1],"\x02\x90\0",3);steps=2;}
  int got=iso_dep_transceive(&dep,command,5,received,20,0);
  CHECK(got==((value>=1&&value<=59)?2:0));
 }
 setup(8,3);add("0A0300B0000001","FA0302");add("FA0302","0A039000");
 CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==2);
 setup(8,3);add("0A0300B0000001","0A049000");CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==0);
 setup(8,0);add("0200B0000001","06009000");CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==0);
 setup(8,0);add("0200B0000001","F201");for(int i=0;i<12;i++)add("F201","F201");
 CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==0);CHECK(calls==9);
 setup(8,0);add("0200B0000001","02AA6102");add("0300C0000002","03BB9000");
 CHECK(iso_dep_apdu_transceive(&dep,command,5,received,20,0)==4);CHECK(!memcmp(received,"\xAA\xBB\x90\0",4));
 setup(8,0);add("0200B0000001","026C02");add("0300B0000002","03AABB9000");
 CHECK(iso_dep_apdu_transceive(&dep,command,5,received,20,0)==4);
 setup(8,0);uint8_t case3[]={0,0xD6,0,0,1,0xAB};add("0200D6000001AB","026C02");
 CHECK(iso_dep_apdu_transceive(&dep,case3,6,received,20,0)==2);CHECK(calls==1);
 setup(8,0);add("0200B0000001","02AA6102");add("0300C0000002","03BB9000");
 CHECK(iso_dep_apdu_transceive(&dep,command,5,received,3,0)==0);CHECK(received[3]==0xA5);
 setup(8,0);add("0200B0000001","026102");
 CHECK(iso_dep_apdu_transceive(&dep,command,5,received,20,0)==0);
 /* Sweep all PCB values: only a complete, in-sequence I-block is data. */
 for(unsigned pcb=0;pcb<256;pcb++){
  setup(8,0);add("0200B0000001","029000");reply[0][0]=pcb;
  if(pcb&8U){reply[0][1]=0;reply[0][2]=0x90;reply[0][3]=0;reply_len[0]=4;}
  int got=iso_dep_transceive(&dep,command,5,received,20,0);
  CHECK(got==((pcb==2U||pcb==10U)?2:0));
 }
 /* A complete 256-byte RF frame carries 253 INF bytes without CID. */
 setup(8,0);memset(long_command,0,sizeof long_command);
 wanted_len[0]=254;wanted[0][0]=2;memset(wanted[0]+1,0,253);
 reply_len[0]=3;memcpy(reply[0],"\x02\x90\0",3);steps=1;
 uint8_t max_command[253]={0};CHECK(iso_dep_transceive(&dep,max_command,253,received,20,0)==2);
 /* ATS advertises absent interface bytes, impossible FSCI, reserved FWI, bad TL. */
 const char*bad[]={"0270","0209","0360F0","0260","0308","0268"};
 for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){clear();add("E080",bad[i]);CHECK(iso_dep_rats(8,0,&dep)!=0);CHECK(!dep.ats_len);}
 clear();add("E080","01");CHECK(iso_dep_rats(8,0,&dep)==0);CHECK(dep.fsc==32&&dep.fwi==4);
 CHECK(iso_dep_rats(9,0,&dep)!=0);CHECK(iso_dep_rats(8,15,&dep)!=0);
 setup(8,0);add("0200B0000001","029000");corrupt_crc=true;
 CHECK(iso_dep_transceive(&dep,command,5,received,20,0)==0);
 clear();add("E080","0208");corrupt_crc=true;
 CHECK(iso_dep_rats(8,0,&dep)!=0);
 printf("PASS: %u assertions; ATS, FSC, sequence, TX/RX chaining, duplicates, CID, WTX, APDU continuation and truncation\n",checks);
}
