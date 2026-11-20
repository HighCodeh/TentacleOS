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
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#define HB_NFC_OK 0
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define HB_NFC_ERR_PARAM -2
#define HB_NFC_ERR_PROTOCOL -3
#define ISO15693_MAX_BLOCK_SIZE 32
#define SLIX_COUNTER_BLOCK 79
#define UL_AES_PATH_SIZE 128
#define UL_AES_DUMP_DIR "/fake"
#define UL_AES_COPY_LIMIT 10
#define ESP_FAIL -4
#define SLIX_LOCK_AFI 1
#define SLIX_LOCK_EAS 2
#define SLIX_LOCK_DSFID 4
#define SLIX_LOCK_PROTECTION 8
#define BLOCKS 80
#define BLOCK_SIZE 4
#define FAILURE -9
typedef int hb_nfc_err_t;
typedef struct {uint8_t uid[8]; uint16_t block_count; uint8_t block_size,dsfid,afi,ic_ref;} iso15693_tag_t;
typedef struct {bool is_read_only,has_system_info,afi_locked,dsfid_locked,eas_locked,protection_locked,has_signature;uint8_t dsfid,afi,ic_ref,protection_pointer,protection_condition,signature[32];bool locked[256],has_password[5];uint8_t passwords[5][4];bool is_slix; uint8_t uid[8]; uint16_t block_count;uint8_t block_size;uint8_t memory[BLOCKS*BLOCK_SIZE];} card_t;
typedef struct {char heading[32];} nfc_protocol_result_t;
typedef struct {uint8_t protection_pointer,protection_condition,lock_bits;} slix_system_info_t;
static bool s_has_slix_password[5];
static uint8_t s_slix_password[5][4];
static char s_slix_last_path[UL_AES_PATH_SIZE];
static bool bad_security;
static unsigned saves;
#undef strlcpy
#define strlcpy test_strlcpy
static size_t test_strlcpy(char *out,const char *in,size_t size){snprintf(out,size,"%s",in);return strlen(in);}
static void format_uid(const uint8_t *uid,size_t len,bool reverse,char *out,size_t size){(void)uid;(void)len;(void)reverse;snprintf(out,size,"E004010000000000");}
static int slix_get_system_info(const iso15693_tag_t *tag,slix_system_info_t *info){(void)tag;info->protection_pointer=12;info->protection_condition=3;info->lock_bits=15;return 0;}
static int slix_read_signature(const iso15693_tag_t *tag,uint8_t *signature){(void)tag;memset(signature,0xA5,32);return 0;}
static int fake_mkdir(const char *path,int mode){(void)path;(void)mode;return 0;}
static int fake_open(const char *path,int flags,int mode){(void)path;(void)flags;(void)mode;return 1;}
static int fake_close(int fd){(void)fd;return 0;}
static int fake_unlink(const char *path){(void)path;return 0;}
#define mkdir fake_mkdir
#define open fake_open
#define close fake_close
#define unlink fake_unlink
static int nfc_v_file_save(const char *path,const card_t *card){
 assert(strstr(path,"slix_") && card->is_slix && card->is_read_only);
 assert(card->has_signature && card->signature[0]==0xA5);
 assert(card->has_system_info && card->protection_pointer==12 && card->protection_condition==3);
 assert(card->afi_locked && card->dsfid_locked && card->eas_locked && card->protection_locked);
 assert(card->has_password[0] && card->passwords[0][0]==0x12);
 assert(card->locked[0]);saves++;return 0;
}
static card_t s_v_card;
static bool s_slix_write_pending,s_has_v,s_v_result_valid;
static char s_slix_write_path[]="slix.nfc";
static uint8_t memory[BLOCKS][BLOCK_SIZE];
static bool cancel,locked,wrong_uid,corrupt,fail_write;
static unsigned writes;
static bool cancel_requested(void){return cancel;}
static bool is_slix2(const iso15693_tag_t *tag){(void)tag;return true;}
static int select_slix(iso15693_tag_t *tag){memset(tag,0,sizeof(*tag));tag->uid[0]=wrong_uid;tag->block_count=BLOCKS;tag->block_size=BLOCK_SIZE;return 0;}
static int nfc_v_file_load(const char *path,card_t *card){(void)path;memset(card,0,sizeof(*card));card->is_slix=true;card->block_count=BLOCKS;card->block_size=BLOCK_SIZE;memset(card->memory,1,sizeof(card->memory));return 0;}
static int iso15693_read_single_block(const iso15693_tag_t *tag,uint8_t block,uint8_t *out,size_t capacity,size_t *length){(void)tag;assert(capacity>=BLOCK_SIZE);memcpy(out,memory[block],BLOCK_SIZE);*length=BLOCK_SIZE;return 0;}
static int iso15693_get_multi_block_sec(const iso15693_tag_t *tag,uint8_t block,uint8_t count,uint8_t *out,size_t capacity,size_t *length){(void)tag;(void)block;assert(count==1 && capacity==1);*out=locked;*length=bad_security?0:1;return 0;}
static int iso15693_write_single_block(const iso15693_tag_t *tag,uint8_t block,const uint8_t *data,size_t length){(void)tag;assert(block!=SLIX_COUNTER_BLOCK && length==BLOCK_SIZE);writes++;if(fail_write)return FAILURE;if(!corrupt)memcpy(memory[block],data,length);return 0;}
static void add_line(nfc_protocol_result_t *result,const char *format,...){(void)result;(void)format;}
/* PRODUCTION */
static int run(void){nfc_protocol_result_t result={0};s_slix_write_pending=true;writes=0;return write_slix(&result);}
int main(void){
 assert(run()==0 && writes==BLOCKS-1 && !s_slix_write_pending);
 assert(memory[SLIX_COUNTER_BLOCK][0]==0);
 assert(run()==0 && writes==0);
 memset(memory,0,sizeof(memory));locked=true;
 assert(run()!=0 && writes==0);locked=false;
 wrong_uid=true;assert(run()!=0 && writes==0);wrong_uid=false;
 cancel=true;assert(run()!=0 && writes==0);cancel=false;
 fail_write=true;assert(run()==FAILURE && writes==1);fail_write=false;
 corrupt=true;assert(run()==HB_NFC_ERR_PROTOCOL && writes==1);corrupt=false;
 nfc_protocol_result_t result={0};s_has_slix_password[0]=true;s_slix_password[0][0]=0x12;
 locked=true;assert(read_slix(&result)==0 && saves==1 && s_has_v);
 bad_security=true;assert(read_slix(&result)!=0 && saves==1 && !s_has_v);bad_security=false;
 cancel=true;assert(read_slix(&result)!=0 && saves==1);cancel=false;
 puts("SLIX capture/restore: matching UID, locked blocks, counter exclusion, verification, cancellation and no retry passed");
}
