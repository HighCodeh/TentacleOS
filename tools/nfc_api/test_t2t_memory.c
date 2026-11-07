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
#include <string.h>
#include <stdio.h>
#define ESP_LOGD(...) ((void)0)
#define T2T_TAG_ULTRALIGHT_AES 9
#define T2T_TAG_NTAG213 0
#define T2T_TAG_NTAG215 1
#define T2T_TAG_NTAG216 2
#define T2T_TAG_NTAG210 7
#define T2T_TAG_NTAG212 8
#define T2T_TAG_ULTRALIGHT_EV1_11 5
#define T2T_TAG_ULTRALIGHT_EV1_21 6
#define T2T_TAG_NTAG_I2C_1K 10
#define T2T_TAG_NTAG_I2C_2K 11
#define T2T_TAG_NTAG_I2C_PLUS_1K 12
#define T2T_TAG_NTAG_I2C_PLUS_2K 13
#define T2T_TAG_COUNT 14
static uint8_t s_i2c_sector;
static bool s_i2c_select_pending;
static void copy_read_page(uint16_t page, uint8_t *out);
static uint16_t get_static_lock_bits(void);
static uint16_t get_dynamic_lock_bits(void);
#define NTAG_ACCESS_PROT 0x80
/* DEFINES */
typedef struct {
  unsigned type, total_pages, user_start, user_end, dyn_lock_page, pwd_page, pack_page, cfg_page;
  bool supports_pwd, supports_ulc_auth, supports_get_version;
} t2t_profile_t;
static t2t_profile_t s_prof;
static uint8_t s_mem[T2T_MEM_SIZE], s_auth0, s_access, s_comp_write_page;
static uint16_t s_page_count;
static struct { uint8_t uid[7]; } s_config;
static uint8_t s_uid_len;
static uint8_t random_seed;
static void esp_fill_random(void *out,size_t n) { memset(out,++random_seed,n); }
static uint8_t s_uid[7] = {4,17,34,51,68,85,102};
static bool s_has_counter, s_counter_increased;
static uint32_t s_counter;
static bool s_has_signature;
static uint8_t s_signature[T2T_SIGNATURE_SIZE];
static uint32_t s_failed_auth_attempts;
static bool s_pwd_enabled, s_pwd_known, s_pack_known;
static uint8_t s_pwd[4]={1,2,3,4},s_pack[2]={5,6};
static uint32_t s_counters[T2T_COUNTER_COUNT];
static uint8_t s_counter_known, s_tearing_known, s_tearing[T2T_COUNTER_COUNT];
static bool is_ev1(void);
static bool is_aes(void);
static struct { bool is_authenticated; } s_aes_session;
static uint16_t s_aes_key_known[T2T_AES_KEY_COUNT];
static bool s_has_aes_signature;
static uint16_t s_aes_signature_known;
static t2t_signature_lock_t s_aes_signature_lock;
static bool s_aes_has_rid;
static uint8_t s_aes_signature[T2T_AES_SIGNATURE_SIZE];
static uint8_t s_ulc_key[T2T_ULC_KEY_LEN];
static uint16_t s_ulc_key_known, s_ulc_counter_next;
static bool s_ulc_key_pending, s_ulc_counter_pending, s_ulc_auth_enabled;
static bool s_authenticated, s_read_only, s_comp_write_pending;
static uint8_t reply[T2T_FAST_READ_MAX_LEN];
static int reply_length, ack;
static void tx_with_crc(const uint8_t *p, int n) { memcpy(reply,p,n); reply_length=n; }
static void tx_4bit_nack(uint8_t n) { (void)n; reply_length=-1; }
static void tx_ack(void) { ack++; }
/* PRODUCTION */
int main(void) {
  uint8_t select[]={1,0,0,0};
  reply_length=0;s_i2c_select_pending=true;i2c_select(select,4);
  assert(s_i2c_sector==1 && !s_i2c_select_pending && reply_length==0 && ack==0);
  select[0]=255;i2c_select(select,4);assert(s_i2c_sector==1 && reply_length==-1);
  select[0]=0;select[3]=1;i2c_select(select,4);assert(s_i2c_sector==1);
  select[3]=0;i2c_select(select,3);assert(s_i2c_sector==1);
  i2c_select(select,4);assert(s_i2c_sector==0);
  const uint16_t counts[] = {231,485,236,492};
  for (unsigned model=0;model<4;model++) {
    s_prof=(t2t_profile_t){.type=T2T_TAG_NTAG_I2C_1K+model};
    s_page_count=counts[model];
    bool seen[492]={false};
    for (unsigned sector=0;sector<4;sector++) {
      for (unsigned page=0;page<256;page++) {
        uint16_t linear=65535;
        if(i2c_address(sector,page,&linear)) {
          assert(linear<s_page_count);seen[linear]=true;
        }
      }
    }
    for(unsigned i=0;i<s_page_count;i++) assert(seen[i]);
    uint16_t mapped;
    assert(!i2c_address(254,0,&mapped));
    assert(!i2c_address(0,256,&mapped));
    assert(i2c_address(3,248,&mapped));
    assert(mapped==(model>=2 ? 234 : counts[model]-2));
    memset(s_mem,0x5A,sizeof(s_mem));
    s_i2c_sector=3;handle_read(248);assert(reply_length==16);
    for(unsigned i=0;i<16;i++)assert(reply[i]==(i<8 ? 0x5A : 0));
    handle_read(250);assert(reply_length==-1);
    s_i2c_sector=0;handle_fast_read(4,7);assert(reply_length==16);
    handle_fast_read(7,4);assert(reply_length==-1);
    handle_fast_read(0,255);assert(reply_length==-1);
    assert(!write_page_internal(4,(const uint8_t[]){1,2,3,4}));
  }
  s_prof=(t2t_profile_t){.type=T2T_TAG_NTAG_I2C_PLUS_2K,.supports_pwd=true,
    .pwd_page=229,.pack_page=230};s_page_count=492;
  memset(s_mem,0,sizeof(s_mem));s_auth0=4;s_access=0x80;s_authenticated=false;
  s_i2c_sector=0;handle_read(4);assert(reply_length==-1);
  s_authenticated=true;handle_read(4);assert(reply_length==16);
  memset(s_mem+229*4,0xFF,8);handle_fast_read(229,230);assert(reply_length==8);
  for(unsigned i=0;i<8;i++)assert(reply[i]==0);
  s_access=0x20;s_i2c_sector=1;handle_read(0);assert(reply_length==-1);
  s_access=0x80;s_mem[231*4]=8;s_authenticated=false;
  handle_read(0);assert(reply_length==-1);
  s_authenticated=true;handle_read(0);assert(reply_length==16);
  s_prof.dyn_lock_page=226;s_prof.user_end=225;
  s_authenticated=false;s_i2c_sector=0;memset(s_mem,0,sizeof(s_mem));
  const uint8_t i2c_data[]={1,2,3,4};
  assert(!write_page_internal(4,i2c_data));
  s_authenticated=true;assert(write_page_internal(4,i2c_data));
  assert(!memcmp(s_mem+16,i2c_data,4));
  s_mem[10]=1U<<4;assert(!write_page_internal(4,i2c_data));s_mem[10]=0;
  s_i2c_sector=1;s_mem[226*4]=0x80;assert(!write_page_internal(0,i2c_data));
  s_mem[226*4]=0;assert(write_page_internal(0,i2c_data));
  assert(!memcmp(s_mem+236*4,i2c_data,4));
  s_i2c_sector=0;assert(!write_page_internal(232,i2c_data));
  s_read_only=true;assert(!write_page_internal(4,i2c_data));s_read_only=false;
  s_authenticated=false;s_i2c_sector=0;memset(s_mem,0,sizeof(s_mem));

  s_prof=(t2t_profile_t){.type=T2T_TAG_ULTRALIGHT_AES,.total_pages=60,.user_start=4,
    .user_end=39,.dyn_lock_page=40,.cfg_page=41,.supports_get_version=true};
  s_page_count=60;s_auth0=4;s_access=0x80;
  uint8_t signature_write[]={0xA9,0,1,2,3,4},signature_lock[]={0xAC,0};
  handle_write_signature(signature_write,6);assert(s_aes_signature_known==0);
  handle_lock_signature(signature_lock,2);assert(s_aes_signature_lock==T2T_SIGNATURE_LOCK_UNKNOWN);
  s_aes_signature_lock=T2T_SIGNATURE_LOCKED;
  handle_write_signature(signature_write,6);assert(s_aes_signature_known==0);
  handle_lock_signature(signature_lock,2);assert(s_aes_signature_lock==T2T_SIGNATURE_UNLOCKED);
  for(unsigned i=0;i<T2T_AES_SIGNATURE_BLOCKS;i++) {
    signature_write[1]=i;handle_write_signature(signature_write,6);
    assert(s_has_aes_signature==(i==T2T_AES_SIGNATURE_BLOCKS-1));
  }
  handle_signature(0,2);assert(reply_length==48 && reply[47]==4);
  signature_write[1]=12;reply_length=0;handle_write_signature(signature_write,6);assert(reply_length==-1);
  signature_write[1]=0;signature_write[2]=9;
  handle_write_signature(signature_write,5);assert(s_aes_signature[0]==1);
  s_read_only=true;handle_write_signature(signature_write,6);assert(s_aes_signature[0]==1);
  signature_lock[1]=2;handle_lock_signature(signature_lock,2);assert(s_aes_signature_lock==T2T_SIGNATURE_UNLOCKED);
  s_read_only=false;signature_lock[1]=3;handle_lock_signature(signature_lock,2);
  assert(s_aes_signature_lock==T2T_SIGNATURE_UNLOCKED);
  signature_lock[1]=1;handle_lock_signature(signature_lock,2);assert(s_aes_signature_lock==T2T_SIGNATURE_LOCKED);
  signature_lock[1]=2;handle_lock_signature(signature_lock,2);assert(s_aes_signature_lock==T2T_SIGNATURE_PERMANENT);
  signature_lock[1]=0;handle_lock_signature(signature_lock,2);handle_write_signature(signature_write,6);
  assert(s_aes_signature_lock==T2T_SIGNATURE_PERMANENT && s_aes_signature[0]==1);
  s_has_aes_signature=false;

  assert(!can_read_page(4));s_authenticated=true;assert(can_read_page(4));
  s_authenticated=false;s_access=0;assert(can_read_page(4));assert(!can_write_page(4));
  s_auth0=60;
  memcpy(s_config.uid,s_uid,7);s_mem[41*4]=T2T_AES_RID_MASK;
  aes_identity();assert(s_uid_len==4 && s_uid[0]==8);uint8_t previous_random=s_uid[1];
  aes_identity();assert(s_uid[1]!=previous_random);
  s_mem[41*4]=0;aes_identity();assert(s_uid_len==7 && !memcmp(s_uid,s_config.uid,7));
  s_aes_has_rid=true;assert(!can_read_page(0));
  s_aes_session.is_authenticated=true;assert(can_read_page(0));
  s_aes_session.is_authenticated=false;s_aes_has_rid=false;
  uint8_t aes_data[4]={1,2,3,4};
  assert(write_page_internal(48,aes_data));assert(s_aes_key_known[0]==15);
  handle_read(48);assert(reply_length==16);
  for(unsigned i=0;i<16;i++)assert(reply[i]==0);
  s_mem[45*4]=0x40;assert(!write_page_internal(48,aes_data));
  s_mem[45*4]=0x20;assert(write_page_internal(45,aes_data));assert(s_mem[45*4]==0x20);
  s_mem[42*4]=0x40;assert(!write_page_internal(41,aes_data));assert(!write_page_internal(42,aes_data));
  assert(!write_page_internal(56,aes_data));
  s_counter_known=7;s_counters[2]=123;s_access=0;s_authenticated=false;
  handle_counter(2);assert(reply_length==-1);
  s_access=T2T_AES_COUNTER_READ;handle_counter(2);assert(reply_length==3 && reply[0]==123);
  uint8_t aes_increment[]={0xA5,2,1,0,0,0};
  handle_increment(aes_increment);assert(s_counters[2]==123);
  s_access|=T2T_AES_COUNTER_INCREMENT;handle_increment(aes_increment);assert(s_counters[2]==124);
  aes_increment[2]=0;s_counters[2]=T2T_COUNTER_MAX;
  handle_increment(aes_increment);assert(s_counters[2]==T2T_COUNTER_MAX);
  s_prof=(t2t_profile_t){0};memset(s_mem,0,sizeof(s_mem));s_counter_known=0;
  s_tearing_known=0;ack=0;s_access=0;
  s_prof=(t2t_profile_t){.type=0,.user_start=4,.user_end=39,.dyn_lock_page=40,
    .pwd_page=43,.pack_page=44,.cfg_page=41,.supports_pwd=true,.supports_get_version=true};
  s_page_count=45; s_auth0=255;
  for (unsigned p=0;p<s_page_count;p++) memset(s_mem+p*4,p,4);
  s_mem[10]=s_mem[11]=s_mem[160]=s_mem[161]=s_mem[162]=0;
  s_mem[167]=255;s_mem[168]=0;
  handle_read(43); assert(reply_length==16);
  for (unsigned i=0;i<8;i++) assert(reply[i]==0);
  assert(reply[8]==0 && reply[12]==1);
  handle_fast_read(42,44); assert(reply_length==12 && reply[0]==s_mem[168] && reply[4]==0);
  handle_read(45); assert(reply_length==-1);
  s_auth0=6;s_access=128;
  handle_read(5); assert(reply_length==16 && reply[0]==5 && reply[4]==0 && reply[8]==1);
  handle_read(6); assert(reply_length==-1);
  handle_fast_read(5,6); assert(reply_length==-1);
  s_authenticated=true;handle_read(6);assert(reply_length==16 && reply[0]==6);
  s_auth0=255;
  uint8_t data[16];memset(data,0xAA,sizeof(data));
  handle_comp_write_start(4);assert(s_comp_write_pending && ack==1);
  handle_comp_write_data(data,16);assert(ack==2 && s_mem[16]==0xAA && s_mem[20]==5);
  s_read_only=true;assert(!write_page_internal(2,data));
  assert(!write_page_internal(40,data));assert(!write_page_internal(4,data));
  s_read_only=false;s_mem[10]=1U<<4;
  assert(!write_page_internal(4,data));assert(write_page_internal(5,data));
  s_mem[160]=1;assert(!write_page_internal(16,data));assert(!write_page_internal(17,data));
  assert(write_page_internal(18,data));
  s_authenticated=false;s_access=T2T_COUNTER_ENABLE;s_has_counter=true;
  s_counter=0x123456;s_counter_increased=false;s_auth0=255;
  handle_counter(2);assert(reply_length==3 && reply[0]==0x56 && reply[2]==0x12);
  handle_counter(1);assert(reply_length==-1);
  s_mem[164]=(T2T_MIRROR_BOTH<<T2T_MIRROR_MODE_SHIFT) | (1<<T2T_MIRROR_OFFSET_SHIFT);
  s_mem[166]=4;
  handle_fast_read(4,9);
  assert(reply_length==24 && !memcmp(reply+1,"04112233445566x123456",21));
  assert(s_counter==0x123457 && s_counter_increased);
  handle_read(4);assert(s_counter==0x123457);
  s_access|=T2T_COUNTER_PROTECTED;
  handle_counter(2);assert(reply_length==-1);
  handle_fast_read(4,9);assert(!memcmp(reply+1,"04112233445566",14));
  assert(reply[15]!= 'x');
  s_mem[164]=T2T_MIRROR_COUNTER<<T2T_MIRROR_MODE_SHIFT;
  handle_read(4);assert(reply[0]==s_mem[16]);
  s_authenticated=true;handle_read(4);assert(!memcmp(reply,"123457",6));
  s_counter=T2T_COUNTER_MAX;s_counter_increased=false;handle_read(4);
  assert(s_counter==T2T_COUNTER_MAX);
  s_mem[166]=39;handle_read(39);assert(reply[0]==s_mem[156]);
  s_mem[166]=3;handle_read(3);assert(reply[0]==s_mem[12]);
  s_mem[166]=4;s_mem[164]=T2T_MIRROR_UID<<T2T_MIRROR_MODE_SHIFT;
  handle_read(5);assert(!memcmp(reply,"2233445566",10));
  s_counter_increased=false;s_access=0;handle_read(4);assert(!s_counter_increased);
  s_prof.type=T2T_TAG_ULTRALIGHT_EV1_11;s_counter_known=7;s_read_only=false;
  s_counters[0]=1;s_counters[1]=5;s_counters[2]=9;
  handle_counter(0);assert(reply_length==3 && reply[0]==1);
  uint8_t increment[]={0xA5,1,3,0,0,0};int prev_ack=ack;
  handle_increment(increment);assert(s_counters[1]==8 && ack==prev_ack+1);
  handle_tearing(1);assert(reply_length==1 && reply[0]==0xBD);
  handle_tearing(2);assert(reply_length==-1);
  s_read_only=true;handle_increment(increment);assert(s_counters[1]==8 && reply_length==-1);
  s_read_only=false;s_counters[1]=T2T_COUNTER_MAX-1;
  handle_increment(increment);assert(s_counters[1]==T2T_COUNTER_MAX-1 && reply_length==-1);
  increment[1]=3;handle_increment(increment);assert(reply_length==-1);
  handle_counter(3);assert(reply_length==-1);
  s_counter_known=0;handle_counter(0);assert(reply_length==-1);
  s_access=2;s_pwd_enabled=true;s_failed_auth_attempts=0;
  const uint8_t wrong[]={0x1B,0,0,0,0},correct[]={0x1B,1,2,3,4};
  handle_pwd_auth(wrong,5);assert(!s_authenticated && s_failed_auth_attempts==1);
  handle_pwd_auth(correct,5);assert(s_authenticated && s_failed_auth_attempts==0 && reply[0]==5);
  handle_pwd_auth(wrong,5);handle_pwd_auth(wrong,5);
  assert(!s_authenticated && s_failed_auth_attempts==2);
  handle_pwd_auth(correct,5);assert(!s_authenticated && reply_length==-1);
  s_access=0;handle_pwd_auth(correct,5);assert(s_authenticated);
  s_pwd_enabled=false;handle_pwd_auth(correct,5);assert(reply_length==-1);
  s_pwd_enabled=false;s_access=0;s_auth0=255;s_mem[10]=T2T_STATIC_LOCK_LOW_FREEZE;s_mem[11]=0;
  uint8_t locks[4]={0,0,0xFF,0xFF};
  assert(write_page_internal(2,locks));assert((s_mem[10]&0xF0)==0 && (s_mem[11]&3)==0);
  s_mem[160]=s_mem[161]=0;s_mem[162]=1;locks[0]=locks[1]=0xFF;
  assert(write_page_internal(40,locks));assert((s_mem[160]&3)==0);
  s_access=T2T_CONFIG_LOCK;
  assert(!write_page_internal(41,locks));assert(!write_page_internal(42,locks));
  memset(s_signature,0xAB,sizeof(s_signature));s_has_signature=true;
  handle_signature(0,2);assert(reply_length==32 && reply[31]==0xAB);
  reply_length=0;handle_signature(0,1);handle_signature(1,2);assert(reply_length==0);
  s_has_signature=false;handle_signature(0,2);assert(reply_length==0);
  s_mem[s_prof.cfg_page*4+T2T_CFG_VCTID_OFFSET]=0x42;
  handle_vcsl(T2T_VCSL_FRAME_LENGTH);assert(reply_length==1 && reply[0]==0x42);
  reply_length=0;handle_vcsl(2);assert(reply_length==0);
  memset(s_mem,0,sizeof(s_mem));
  s_prof=(t2t_profile_t){.supports_ulc_auth=true,.user_start=4,.user_end=39,.dyn_lock_page=40};
  s_page_count=48;s_authenticated=false;
  s_mem[42*4]=20;s_mem[43*4]=0;
  assert(can_read_page(19) && !can_read_page(20));
  assert(!can_write_page(20));
  s_mem[43*4]=1;assert(can_read_page(20) && !can_write_page(20));
  s_authenticated=true;assert(can_read_page(43) && !can_read_page(44));
  assert(!can_read_page(47));
  handle_read(43);assert(reply_length==16 && reply[4]==s_mem[0]);
  s_authenticated=false;s_mem[42*4]=48;assert(can_read_page(43));
  memset(s_mem,0,sizeof(s_mem));s_mem[42*4]=48;s_read_only=false;
  for (unsigned group=0;group<6;group++) {
    s_mem[160]=1U<<(group+1+(group>=3));
    for(unsigned page=16;page<40;page++) assert(is_page_locked(page)==((page-16)/4==group));
  }
  s_mem[160]=1;s_mem[161]=0;
  uint8_t ulc_lock[4]={0x0E,0,0xFF,0xFF};
  assert(write_page_internal(40,ulc_lock));assert(s_mem[160]==1 && s_mem[162]==0);
  s_mem[161]=0x80;assert(is_page_locked(44) && is_page_locked(47));
  assert(!is_page_locked(43));
  copy_read_page(40,reply);assert(reply[3]==0xBD);
  s_mem[161]=0;
  uint8_t counter_data[4]={0xF0,0xFF,0,0};
  assert(write_page_internal(41,counter_data));assert(s_mem[164]==0);
  ulc_activate(false);assert(s_mem[164]==0);
  ulc_activate(true);assert(s_mem[164]==0xF0 && s_mem[165]==0xFF);
  counter_data[0]=0xFF;counter_data[1]=0xFF;
  assert(write_page_internal(41,counter_data));ulc_activate(true);
  assert(s_mem[164]==0xFF && s_mem[165]==0xFF);
  assert(!write_page_internal(41,counter_data));
  counter_data[0]=0xF0;assert(write_page_internal(41,counter_data));
  ulc_activate(true);assert(s_mem[164]==0xFF);
  for(unsigned page=44;page<48;page++) {
    uint8_t physical[4];
    for(unsigned i=0;i<4;i++) physical[i]=((page-44)*4+i)^7;
    assert(write_page_internal(page,physical));
    assert(!s_ulc_auth_enabled);
    if(page==44) { ulc_activate(false);assert(!s_ulc_auth_enabled); }
  }
  ulc_activate(false);assert(s_ulc_auth_enabled);
  for(unsigned i=0;i<16;i++) assert(s_ulc_key[i]==i);
  s_read_only=true;assert(!write_page_internal(44,counter_data));
  assert(!write_page_internal(41,counter_data));
  s_prof=(t2t_profile_t){.type=T2T_TAG_NTAG210,.user_start=4,.user_end=15,
    .cfg_page=16,.pwd_page=18,.pack_page=19,.dyn_lock_page=256,.supports_pwd=true,
    .supports_get_version=true};
  memset(s_mem,0,sizeof(s_mem));s_page_count=20;s_auth0=255;s_access=0;
  s_mem[66]=4;s_mem[64]=1<<T2T_MIRROR_OFFSET_SHIFT;
  handle_read(4);assert(reply_length==16 && !memcmp(reply+1,"04112233445566",14));
  handle_counter(2);assert(reply_length==-1);
  s_has_signature=true;handle_signature(0,2);assert(reply_length==32);
  s_prof.type=T2T_TAG_NTAG212;s_prof.user_end=35;s_prof.dyn_lock_page=36;
  s_prof.cfg_page=37;s_prof.pwd_page=39;s_prof.pack_page=40;s_page_count=41;
  s_mem[144]=2;assert(is_page_locked(18) && is_page_locked(19) && !is_page_locked(20));
  s_read_only=false;s_authenticated=false;s_access=0;s_auth0=255;
  s_mem[37*4+3]=255;s_mem[38*4]=0;
  s_pwd_known=s_pack_known=s_pwd_enabled=false;
  const uint8_t password[4]={9,8,7,6},pack[4]={5,4,0,0};
  assert(write_page_internal(39,password));assert(!s_pwd_enabled);
  assert(write_page_internal(40,pack));assert(s_pwd_enabled);
  uint8_t auth[5]={0x1B,9,8,7,6};handle_pwd_auth(auth,5);
  assert(s_authenticated && reply_length==2 && reply[0]==5 && reply[1]==4);
  puts("Type 2 memory: rollover, secret masking, access, compatible write and locks passed");
}
