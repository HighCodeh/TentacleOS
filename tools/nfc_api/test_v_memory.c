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
#define HB_NFC_ERR_PROTOCOL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_SIZE -3
#define HB_PROTO_SLIX 1
#define NFC_STORE_PAYLOAD_MAX 2048
#define NFC_STORE_NAME_MAX 32
#define ISO15693_MAX_BLOCKS 256
#define ISO15693_MAX_BLOCK_SIZE 32
#define add_line(result, ...) ((void)(result))
#define format_uid(uid, len, reverse, out, max) (memset(out, 0, max))
typedef int hb_nfc_err_t;
typedef int hb_nfc_protocol_t;
typedef struct { uint8_t uid[8], dsfid, afi, ic_ref, block_size;
 uint16_t block_count; bool info_valid; } iso15693_tag_t;
typedef struct { int unused; } nfc_protocol_result_t;
typedef struct { const char *name;int protocol;const uint8_t *uid;size_t uid_len;
 const uint8_t *payload;size_t payload_len; } nfc_store_card_t;
static unsigned reads, saves;
static int failed_block = -1;
static bool cancelled, short_read;
static bool cancel_requested(void) { return cancelled; }
static void nfc_store_init(void) {}
static int nfc_store_save(const nfc_store_card_t *card) {
 saves++;
 assert(card->payload[0]==1 && card->payload_len==8+reads*4);
 assert((unsigned)(card->payload[1]|card->payload[2]<<8)==reads);
 for(unsigned b=0;b<reads;b++) for(unsigned i=0;i<4;i++)
   assert(card->payload[8+b*4+i]==(uint8_t)b);
 return 0;
}
static int iso15693_read_single_block(const iso15693_tag_t *tag,uint8_t block,
 uint8_t *out,size_t max,size_t *len) {
 assert(max==tag->block_size && block==reads++);
 if(block==failed_block) return -9;
 memset(out,block,max); *len=max-(short_read?1:0); return 0;
}
typedef struct {const char *name;const uint8_t *data;size_t length;} nfc_archive_field_t;
static int save_capture(const char *device,const uint8_t *uid,size_t uid_len,
 const nfc_archive_field_t *fields,size_t count,nfc_protocol_result_t *result) {
 (void)uid;(void)result; assert(!strcmp(device,"ISO15693-3") && uid_len==8 && count==2);assert(fields[1].length==reads*4); return 0;
}
/* PRODUCTION */
int main(void) {
 iso15693_tag_t tag={.block_size=4,.block_count=256,.info_valid=true};
 nfc_protocol_result_t result;
 assert(save_v_memory(&tag,HB_PROTO_SLIX,&result)==0 && reads==256 && saves==1);
 for(int block=0;block<256;block++) {
  reads=saves=0;failed_block=block;
  assert(save_v_memory(&tag,HB_PROTO_SLIX,&result)==-9 && saves==0);
 }
 failed_block=-1;reads=saves=0;short_read=true;
 assert(save_v_memory(&tag,HB_PROTO_SLIX,&result)==HB_NFC_ERR_PROTOCOL && saves==0);
 short_read=false;cancelled=true;reads=0;
 assert(save_v_memory(&tag,HB_PROTO_SLIX,&result)==ESP_ERR_INVALID_STATE && reads==0);
 cancelled=false;tag.block_size=32;
 assert(save_v_memory(&tag,HB_PROTO_SLIX,&result)==ESP_ERR_INVALID_SIZE && reads==0);
 tag.block_size=0;
 assert(save_v_memory(&tag,HB_PROTO_SLIX,&result)==HB_NFC_ERR_PROTOCOL);
 puts("PASS: NFC-V full memory persistence, every failed block, short reads and bounds");
}
