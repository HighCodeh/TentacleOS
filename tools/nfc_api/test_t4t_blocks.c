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
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#define T4T_STATE_SLEEP 0
#define T4T_FILE_NONE 0
#define ST25R3916_CMD_GOTO_SLEEP 1
/* DEFINES */
static uint8_t s_chain_buf[T4T_CHAIN_BUF_SIZE], s_last_resp[T4T_MAX_RESP_SIZE];
static size_t s_chain_len;
static int s_last_resp_len, s_state, s_file;
static bool s_chain_active, s_app_selected, s_has_rats;
static uint8_t s_expected_block, s_pcd_cid;
static uint16_t s_frame_size = T4T_FRAME_MAX;
static uint8_t s_tx_data[T4T_MAX_RESP_SIZE], s_tx_pcb;
static size_t s_tx_length, s_tx_offset;
static unsigned s_active_tick, calls, sends, resets;
static size_t received;
static uint8_t transmitted[T4T_MAX_RESP_SIZE];
static int transmitted_len;
static unsigned xTaskGetTickCount(void) { return 10; }
static void reset_application(void) { resets++; }
static void hb_nfc_spi_direct_cmd(int c) { (void)c; }
static void tx_with_crc(const uint8_t *p, int n) {
  assert(n > 0 && n <= T4T_MAX_RESP_SIZE);
  memcpy(transmitted, p, (size_t)n); transmitted_len=n; sends++;
}
static void send_i_block_resp(const uint8_t *, int, uint8_t);
static void handle_apdu(const uint8_t *p, int n, uint8_t pcb) {
  (void)p; calls++; received=(size_t)n;
  const uint8_t sw[]={0x90,0}; send_i_block_resp(sw,2,pcb);
}
/* PRODUCTION */
static void reset(void) {
  s_chain_len=0; s_last_resp_len=0; s_expected_block=0; s_pcd_cid=0;
  calls=sends=resets=0; s_state=1; s_tx_length=s_tx_offset=0;s_frame_size=T4T_FRAME_MAX;
}
int main(void) {
  reset();
  uint8_t truncated[]={0x0a}; process_block(truncated,1); assert(calls==0 && sends==0);
  uint8_t nad[]={0x06,0,1}; process_block(nad,3); assert(calls==0);
  uint8_t first[]={0x12,1,2}; process_block(first,3);
  assert(s_chain_len==2 && transmitted[0]==0xa3);
  process_block(first,3); assert(s_chain_len==2 && calls==0 && sends==2);
  uint8_t last[]={0x03,3}; process_block(last,2);
  assert(calls==1 && received==3 && transmitted[0]==3);
  process_block(last,2); assert(calls==1 && sends==4);
  uint8_t ack[]={0xa2}; process_block(ack,1); assert(sends==4);
  uint8_t nak[]={0xb3}; process_block(nak,1); assert(sends==5);
  reset(); s_pcd_cid=4;
  uint8_t cid[]={0x0a,4,1}; process_block(cid,3);
  assert(calls==1 && transmitted[0]==0x0a && transmitted[1]==4 && transmitted_len==4);
  reset();
  uint8_t chunk[T4T_CHAIN_BUF_SIZE+1]; memset(chunk,0,sizeof(chunk)); chunk[0]=0x12;
  process_block(chunk,sizeof(chunk)); assert(s_chain_len==T4T_CHAIN_BUF_SIZE);
  uint8_t end[]={0x03}; process_block(end,1);
  assert(calls==1 && received==T4T_CHAIN_BUF_SIZE);
  reset(); process_block(chunk,sizeof(chunk));
  process_block(last,2); assert(calls==0 && resets==1 && s_state==T4T_STATE_SLEEP);
  reset();
  uint8_t response[T4T_MAX_RESP_SIZE]; memset(response,0,sizeof(response));
  send_i_block_resp(response,T4T_MAX_RESP_SIZE,2);
  assert(transmitted_len==254 && (transmitted[0]&0x10));
  assert(s_tx_offset==253 && s_tx_length==260);
  uint8_t next[]={0xa3}; process_block(next,1);
  assert(transmitted_len==8 && !(transmitted[0]&0x10) && s_tx_offset==260);
  reset();s_frame_size=16;s_pcd_cid=4;
  for(size_t i=0;i<sizeof(response);i++)response[i]=(uint8_t)i;
  send_i_block_resp(response,30,0x0a);
  assert(transmitted_len==14 && transmitted[0]==0x1a && transmitted[1]==4);
  assert(memcmp(transmitted+2,response,12)==0);
  uint8_t cid_ack[]={0xab,4};process_block(cid_ack,2);
  assert(transmitted[0]==0x1b && memcmp(transmitted+2,response+12,12)==0);
  process_block(cid_ack,2);assert(memcmp(transmitted+2,response+12,12)==0 && s_tx_offset==24);
  cid_ack[0]=0xaa;process_block(cid_ack,2);
  assert(transmitted[0]==0x0a && transmitted_len==8 && memcmp(transmitted+2,response+24,6)==0);
  sends=0;send_i_block_resp(response,T4T_MAX_RESP_SIZE+1,2);assert(sends==0);
  puts("T4T block bounds, duplicate suppression, chaining and CID: OK");
}
