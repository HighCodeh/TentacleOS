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
#include <limits.h>
/* CONSTANTS */
#define ESP_OK 0
#define HB_NFC_OK 0
#define ST25R3916_CMD_UNMASK_RX_DATA 0xD1
#define MFC_EMU_EVT_VALUE_OP 1
#define ST25R3916_REG_ISO14443A 5
#define ST25R3916_ISO14443A_NO_TX_PAR 0x80
#define ST25R3916_ISO14443A_NO_RX_PAR 0x40
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
typedef int hb_nfc_err_t;
typedef enum {MFC_EMU_STATE_AUTHENTICATED, MFC_EMU_STATE_ERROR} mfc_emu_state_t;
static struct { uint8_t pending_main; uint32_t auth_nt;
 struct {int total_blocks; uint8_t blocks[256][16];} card;
 struct {unsigned nacks_sent,value_ops;} stats;
 int auth_sector,auth_key_type; uint8_t pending_cmd,pending_block;
 int crypto; bool crypto_active;
 int32_t pending_value; bool has_pending_value; int64_t last_activity_us;
} s_emu;
static struct {struct {uint8_t cmd,block;int32_t value;} value_op;} s_evt;
static unsigned s_pending_irq;
static void crypto1_reset(int *crypto){*crypto=0;}
static void hb_nfc_spi_reg_modify(int reg,int mask,int bits){assert(reg==5&&mask==0xC0&&bits==0);}
static unsigned rearmed,acks; static uint8_t ack; static bool bus_fail,can_transfer;
static int hb_nfc_spi_direct_cmd(uint8_t cmd){assert(cmd==0xD1);rearmed++;return bus_fail?-1:0;}
static int target_tx_ack_encrypted(uint8_t value){ack=value;acks++;return 0;}
static int block_to_sector(uint8_t block){return block/4;}
static int sector_trailer_block(int sector){return sector*4+3;}
static int block_index_in_sector(uint8_t block){return block%4;}
static const uint8_t *get_trailer_for_block(uint8_t block){return s_emu.card.blocks[sector_trailer_block(block_to_sector(block))];}
static bool mfc_emu_can_decrement(const uint8_t *trailer,int group,int key){(void)trailer;(void)group;(void)key;return can_transfer;}
static void emit_event(int event){assert(event==1);}
static int64_t esp_timer_get_time(void){return 1234;}
static uint16_t crc_a(const uint8_t *p,size_t n){
 uint16_t crc=0x6363;
 while(n--){crc^=*p++;for(int i=0;i<8;i++)crc=(crc>>1)^((crc&1)?0x8408:0);}
 return crc;
}
static bool iso14443a_check_crc(const uint8_t *p,int n){uint16_t c=crc_a(p,n-2);return p[n-2]==(uint8_t)c&&p[n-1]==(uint8_t)(c>>8);}
/* PRODUCTION */
static void operand_frame(uint8_t frame[6],uint32_t value){
 for(int i=0;i<4;i++)frame[i]=value>>(8*i);
 uint16_t crc=crc_a(frame,4);frame[4]=crc;frame[5]=crc>>8;
}
static void reset(void){memset(&s_emu,0,sizeof(s_emu));s_emu.card.total_blocks=64;s_emu.auth_sector=1;s_emu.pending_block=4;s_emu.pending_cmd=MFC_CMD_INCREMENT;rearmed=acks=0;bus_fail=false;can_transfer=true;write_value_to_block(s_emu.card.blocks[4],1000,4);}
int main(void){
 uint8_t data[6],before[16]; reset();operand_frame(data,25);
 assert(handle_value_op_phase2(data,6)==MFC_EMU_STATE_AUTHENTICATED);
 assert(s_emu.has_pending_value&&s_emu.pending_value==1025&&rearmed==1&&acks==0);
 assert(read_value_from_block(s_emu.card.blocks[4])==1000);
 assert(handle_transfer(4)==MFC_EMU_STATE_AUTHENTICATED&&ack==MFC_ACK);
 assert(read_value_from_block(s_emu.card.blocks[4])==1025&&is_value_block_format(s_emu.card.blocks[4]));
 s_emu.pending_cmd=MFC_CMD_DECREMENT;operand_frame(data,10);
 assert(handle_value_op_phase2(data,6)==MFC_EMU_STATE_AUTHENTICATED&&s_emu.pending_value==1015);
 assert(handle_transfer(4)==MFC_EMU_STATE_AUTHENTICATED);
 s_emu.pending_cmd=MFC_CMD_RESTORE;operand_frame(data,123);
 assert(handle_value_op_phase2(data,6)==MFC_EMU_STATE_AUTHENTICATED&&s_emu.pending_value==1015);
 assert(handle_transfer(5)==MFC_EMU_STATE_AUTHENTICATED&&read_value_from_block(s_emu.card.blocks[5])==1015);
 for(int n=0;n<=8;n++){if(n==6)continue;reset();assert(handle_value_op_phase2(data,n)==MFC_EMU_STATE_AUTHENTICATED);assert(!s_emu.has_pending_value&&ack==MFC_NACK_PARITY_CRC);}
 for(int bit=0;bit<48;bit++){reset();operand_frame(data,25);data[bit/8]^=1U<<(bit%8);handle_value_op_phase2(data,6);assert(!s_emu.has_pending_value&&ack==MFC_NACK_PARITY_CRC);}
 reset();operand_frame(data,1);write_value_to_block(s_emu.card.blocks[4],INT32_MAX,4);
 handle_value_op_phase2(data,6);assert(s_emu.pending_value==INT32_MIN);
 write_value_to_block(s_emu.card.blocks[4],INT32_MIN,4);s_emu.pending_cmd=MFC_CMD_DECREMENT;
 handle_value_op_phase2(data,6);assert(s_emu.pending_value==INT32_MAX);
 reset();bus_fail=true;operand_frame(data,25);assert(handle_value_op_phase2(data,6)==MFC_EMU_STATE_ERROR&&!s_emu.has_pending_value);
 for(int block=0;block<70;block++){
  reset();s_emu.has_pending_value=true;s_emu.pending_value=22;
  memcpy(before,s_emu.card.blocks[block],16);handle_transfer(block);
  if(block>=4&&block<=6)assert(ack==MFC_ACK);else assert(ack==MFC_NACK_INVALID_OP&&!memcmp(before,s_emu.card.blocks[block],16));
 }
 reset();memcpy(before,s_emu.card.blocks[4],16);handle_transfer(4);assert(ack==MFC_NACK_INVALID_OP&&!memcmp(before,s_emu.card.blocks[4],16));
 s_emu.has_pending_value=true;can_transfer=false;handle_transfer(4);assert(ack==MFC_NACK_INVALID_OP&&!memcmp(before,s_emu.card.blocks[4],16));
 reset();s_emu.has_pending_value=true;s_emu.crypto_active=true;s_pending_irq=1;reset_crypto_state();assert(!s_emu.has_pending_value&&!s_emu.crypto_active&&s_pending_irq==0);
 puts("Classic value: CRC/length, silent RX rearm, INC/DEC/RESTORE/TRANSFER, overflow, denied destinations and bus faults PASS (host only)");
}
