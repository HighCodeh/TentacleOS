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
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
#define HB_NFC_OK 0
#define HB_NFC_ERR_AUTH -4
#define HB_NFC_ERR_PARAM -5
#define HB_NFC_ERR_INTERNAL -6
#define HB_NFC_ERR_NOT_FOUND -7
#define HB_NFC_ERR_FIELD -8
#define HB_NFC_ERR_PROTOCOL -9
#define MF_CLASSIC_MINI 0
#define MF_CLASSIC_1K 1
#define MF_CLASSIC_4K 2
#define MF_KEY_A 0x60
#define MF_KEY_B 0x61
#define MF_4K_BIG_SECTOR_START 32
#define MF_4K_BIG_BLOCK_BASE 128
#define MF_4K_BIG_BLOCK_COUNT 16
#define MF_SMALL_BLOCK_COUNT 4
#define TOS_PATH_NFC "."
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef int hb_nfc_err_t, esp_err_t, mf_key_type_t;
typedef struct { uint8_t data[6]; } mf_classic_key_t;
typedef struct { uint8_t uid[10], uid_len, atqa[2], sak; } nfc_iso14443a_data_t;
typedef struct { const uint8_t *uid; int uid_len, sector, sector_count, type; const uint8_t *key; } mf_key_cache_store_params_t;
/* HEADER */
static int power_count, field_count, reads, cancelled_after, bad_block, auths, latest_block;
static bool b_readable, transient_a, cancel_next_sector;
static int first_a_attempts, last_type, selected_sector;
static uint8_t expected_cuid[4];
static esp_err_t power_manager_no_sleep_acquire(void) { power_count++;return ESP_OK; }
static esp_err_t power_manager_no_sleep_release(void) { power_count--;return ESP_OK; }
static void mf_key_dict_init(void) {}
static void mf_key_cache_init(void) {}
static void mf_key_cache_save(void) {}
static void mf_key_cache_store(const mf_key_cache_store_params_t *p) { assert(p->sector < p->sector_count); }
static bool mf_key_cache_lookup(const uint8_t *uid,int len,int sector,int type,uint8_t *key) {
 (void)uid;(void)len;(void)sector;(void)type;(void)key;return false;
}
static int mf_key_dict_count(void) {return 1;}
static void mf_key_dict_get(int index,uint8_t *key) {(void)index;memset(key,0xff,6);}
static hb_nfc_err_t nfc_poller_start(void) {field_count++;return HB_NFC_OK;}
static void nfc_poller_stop(void) {field_count--;}
static void vTaskDelay(int ms) {(void)ms;}
static void mf_classic_reset_auth(void) {}
static hb_nfc_err_t iso14443a_poller_reselect(nfc_iso14443a_data_t *card) {(void)card;return HB_NFC_OK;}
static hb_nfc_err_t mf_classic_auth(uint8_t block,int type,const mf_classic_key_t *key,const uint8_t *uid) {
 assert(memcmp(uid,expected_cuid,4)==0);assert(key->data[0]==0xff);auths++;latest_block=block;last_type=type;selected_sector=nfc_mfc_dump_block_sector(block);
 if(transient_a && block==0 && type==MF_KEY_A && first_a_attempts++<2)return HB_NFC_ERR_AUTH;
 return b_readable && type==MF_KEY_B ? HB_NFC_ERR_AUTH : HB_NFC_OK;
}
static hb_nfc_err_t mf_classic_read_block(uint8_t block,uint8_t *data) {
 assert(block==latest_block);reads++;
 if(block==bad_block || (transient_a && last_type==MF_KEY_B)) return HB_NFC_ERR_PROTOCOL;
 memset(data,block,16);
 int sector=nfc_mfc_dump_block_sector(block);
 if(b_readable && block==nfc_mfc_dump_first_block(sector)+nfc_mfc_dump_sector_blocks(sector)-1) {
   data[6]=0xff;data[7]=0x07;data[8]=0x80;memset(data+10,0xff,6);
 }
 return HB_NFC_OK;
}
static int mf_classic_get_type(uint8_t sak) {return sak==9?MF_CLASSIC_MINI:sak==0x18?MF_CLASSIC_4K:MF_CLASSIC_1K;}
static int mf_classic_get_sector_count(int type) {return type==MF_CLASSIC_MINI?5:type==MF_CLASSIC_4K?40:16;}
static esp_err_t storage_mkdir_recursive(const char *path) {mkdir(path,0700);return ESP_OK;}
static bool fail_bin_open;
static int mock_open(const char *path, int flags, mode_t mode) {
 if (fail_bin_open && strstr(path, ".bin")) return -1;
 return open(path, flags, mode);
}
#define open mock_open
/* PRODUCTION */
#undef open
static bool cancel(void *context) {(void)context;return (cancelled_after>=0 && reads>=cancelled_after) || (cancel_next_sector && selected_sector>0);}
static void reset(nfc_iso14443a_data_t *card) {
 memcpy(expected_cuid,card->uid+card->uid_len-4,4);
 power_count=field_count=reads=auths=0;cancelled_after=-1;bad_block=-1;b_readable=transient_a=cancel_next_sector=false;first_a_attempts=selected_sector=0;
}
static char *load(const char *path) {
 FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 char *data=calloc(n+1,1);assert(data);assert(fread(data,1,n,f)==(size_t)n);fclose(f);return data;
}
int main(void) {
 const nfc_mfc_dump_callbacks_t callbacks={.cancel=cancel};
 static nfc_mfc_dump_result_t dump;
 nfc_iso14443a_data_t card={.uid={0x04,0x11,0x22,0x33,0x44,0x55,0x66},.uid_len=7,.atqa={4,0}};
 const uint8_t sak[]={9,8,0x18};const int blocks[]={20,64,256};const int sectors[]={5,16,40};
 assert(nfc_mfc_dump_first_block(31)==124 && nfc_mfc_dump_first_block(32)==128);
 assert(nfc_mfc_dump_first_block(39)==240 && nfc_mfc_dump_block_sector(255)==39);
 for(unsigned i=0;i<3;i++) {
  card.sak=sak[i];reset(&card);
  assert(nfc_mfc_dump_read(&card,&callbacks,&dump)==HB_NFC_OK);
  assert(power_count==0 && field_count==0);
  assert(dump.total_blocks==blocks[i] && dump.blocks_read==blocks[i]);
  assert(dump.sector_count==sectors[i] && dump.sectors_complete==sectors[i]);
  assert(dump.keys_a_found==sectors[i] && dump.keys_b_found==sectors[i] && dump.emulation_ready);
  fail_bin_open=true;
  assert(nfc_mfc_dump_save(&card,&dump)==ESP_FAIL && !dump.saved);
  assert(access(dump.nfc_path,F_OK)!=0 && access(dump.bin_path,F_OK)!=0);
  fail_bin_open=false;
  assert(nfc_mfc_dump_save(&card,&dump)==ESP_OK && dump.saved);
  struct stat info;assert(stat(dump.bin_path,&info)==0 && info.st_size==blocks[i]*16);
  char *profile=load(dump.nfc_path);assert(!strstr(profile,"??"));free(profile);
 }
 card.sak=8;reset(&card);b_readable=true;
 assert(nfc_mfc_dump_read(&card,&callbacks,&dump)==HB_NFC_OK);
 assert(dump.keys_b_found==16 && dump.emulation_ready);
 reset(&card);bad_block=17;
 assert(nfc_mfc_dump_read(&card,&callbacks,&dump)==HB_NFC_ERR_PROTOCOL);
 assert(dump.blocks_read==63 && !dump.emulation_ready && !dump.block_read[17]);
 assert(nfc_mfc_dump_save(&card,&dump)==ESP_OK && dump.bin_path[0]==0);
 char saved[128];strcpy(saved,dump.nfc_path);char *profile=load(saved);
 assert(strstr(profile,"Block 17: ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ??"));free(profile);
 assert(nfc_mfc_dump_save(&card,&dump)==ESP_OK && strcmp(saved,dump.nfc_path)!=0);
 reset(&card);cancelled_after=6;
 assert(nfc_mfc_dump_read(&card,&callbacks,&dump)==ESP_ERR_INVALID_STATE);
 assert(dump.cancelled && dump.blocks_read==6 && dump.sectors_complete==1);
 assert(power_count==0 && field_count==0);
 assert(nfc_mfc_dump_save(&card,&dump)==ESP_OK && dump.saved);
 reset(&card);transient_a=true;cancel_next_sector=true;
 assert(nfc_mfc_dump_read(&card,&callbacks,&dump)==ESP_ERR_INVALID_STATE);
 assert(dump.cancelled && dump.blocks_read==4 && dump.key_a_found[0]);
 card.uid_len=3;assert(nfc_mfc_dump_read(&card,NULL,&dump)==HB_NFC_ERR_PARAM);
 puts("PASS: Mini/1K/4K geometry, 7-byte CUID, readable Key B, partial and cancelled reads, cleanup, unknown-byte export and non-overwriting saves");
}
