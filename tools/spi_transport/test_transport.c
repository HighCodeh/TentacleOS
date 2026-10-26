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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "spi_protocol.h"
#define DMA_ATTR
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT -2
#define ESP_ERR_INVALID_STATE -3
#define SPI_POLL_SLOW_MS 2
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t;
static int64_t clock_us;
static uint64_t random_value=100, peer_boot=200, peer_master;
static uint32_t peer_seq;
static unsigned executions, commands_sent, syncs;
static size_t peer_cached_size;
static uint8_t peer_cache[SPI_TRANSPORT_FRAME_SIZE], peer_response[SPI_TRANSPORT_STREAM_SIZE];
static bool pending, drop_request, corrupt_reply, stale_reply, suppress_peer, recover_reply;
static int64_t esp_timer_get_time(void) {return clock_us;}
static void esp_fill_random(void *out,size_t n) {assert(n==8);random_value++;memcpy(out,&random_value,n);}
static void vTaskDelay(uint32_t ms) {clock_us+=(int64_t)ms*1000;}
static esp_err_t spi_bridge_phy_transmit(const uint8_t *,uint8_t *,size_t);

uint16_t esp_rom_crc16_le(uint16_t crc,const uint8_t *bytes,uint32_t length) {
  crc=(uint16_t)~crc;
  for(uint32_t i=0;i<length;i++) {
    crc^=bytes[i];
    for(unsigned bit=0;bit<8;bit++)crc=(uint16_t)((crc>>1)^((crc&1)?0x8408:0));
  }
  return (uint16_t)~crc;
}

/* PRODUCTION */
static esp_err_t spi_bridge_phy_transmit(const uint8_t *tx,uint8_t *rx,size_t n) {
  clock_us+=100;
  if(rx)memset(rx,0,n);
  if(suppress_peer)return ESP_OK;
  if(pending) {
    if(rx) {
      size_t count=SPI_TRANSPORT_HEADER_SIZE+peer_cached_size;
      memcpy(rx,peer_response,count<n?count:n);
      if(recover_reply) {
        spi_transport_seal(rx,SPI_FRAME_SIZE,SPI_TRANSPORT_RECOVER,s_master_session,0,UINT32_MAX);
        recover_reply=false;
      }
      if(corrupt_reply){rx[0]^=1;corrupt_reply=false;}
      if(stale_reply) {
        spi_transport_header_t old;memcpy(&old,rx,sizeof(old));
        spi_transport_seal(rx,old.frame_size,SPI_TRANSPORT_RESPONSE,old.master_session,
                           old.slave_session,old.sequence-1);
        stale_reply=false;
      }
    }
    pending=false;
    return ESP_OK;
  }
  if(n<SPI_TRANSPORT_FRAME_SIZE)return ESP_OK;
  spi_transport_header_t request;
  spi_transport_action_t action=transport_classify(tx,peer_master,peer_boot,peer_seq,
                                                   peer_cache,peer_cached_size,&request);
  if(action==SPI_TRANSPORT_DROP)return ESP_OK;
  if(action==SPI_TRANSPORT_CACHED){pending=true;return ESP_OK;}
  if(action==SPI_TRANSPORT_EXECUTE) {
    commands_sent++;
    if(drop_request){drop_request=false;return ESP_OK;}
    executions++;
  } else {peer_master=request.master_session;syncs++;}
  peer_seq=request.sequence;
  memcpy(peer_cache,tx,SPI_TRANSPORT_FRAME_SIZE);
  peer_cached_size=SPI_FRAME_SIZE;
  memset(peer_response,0,sizeof(peer_response));
  memcpy(peer_response+SPI_TRANSPORT_HEADER_SIZE,tx+SPI_TRANSPORT_HEADER_SIZE,SPI_FRAME_SIZE);
  spi_header_t *reply=(spi_header_t *)(peer_response+SPI_TRANSPORT_HEADER_SIZE);
  reply->type=SPI_TYPE_RESP;reply->length=1;
  peer_response[SPI_TRANSPORT_HEADER_SIZE+sizeof(*reply)]=0;
  spi_frame_seal(reply,1);
  spi_transport_seal(peer_response,SPI_FRAME_SIZE,SPI_TRANSPORT_RESPONSE,peer_master,peer_boot,peer_seq);
  pending=true;
  return ESP_OK;
}
static void command(uint8_t *frame) {
  memset(frame,0,SPI_FRAME_SIZE);
  spi_header_t *h=(spi_header_t *)frame;
  h->sync=SPI_SYNC_BYTE;h->type=SPI_TYPE_CMD;h->category=0;h->op=1;
  spi_frame_seal(h,0);
}
int main(void) {
  _Static_assert(sizeof(spi_transport_header_t)==SPI_TRANSPORT_HEADER_SIZE,"envelope size");
  _Static_assert(!(SPI_TRANSPORT_FRAME_SIZE%4),"DMA command alignment");
  _Static_assert(!(SPI_TRANSPORT_STREAM_SIZE%4),"DMA stream alignment");
  uint8_t frame[SPI_FRAME_SIZE], reply[SPI_FRAME_SIZE], saved[SPI_TRANSPORT_FRAME_SIZE];
  command(frame);
  assert(transport_send(frame,clock_us+100000)==ESP_OK);
  assert(executions==1 && syncs==1);
  memcpy(saved,peer_cache,sizeof(saved));
  assert(transport_read(reply,sizeof(reply))==ESP_OK && reply[0]==SPI_SYNC_BYTE);
  // Exact duplicate returns the cache without dispatching twice.
  spi_bridge_phy_transmit(saved,NULL,sizeof(saved));
  transport_read(reply,sizeof(reply));assert(reply[0]==SPI_SYNC_BYTE && executions==1);
  // A late response to the same command cannot satisfy a new transaction.
  spi_bridge_phy_transmit(saved,NULL,sizeof(saved));
  assert(transport_send(frame,clock_us+100000)==ESP_OK);
  assert(transport_read(reply,sizeof(reply))==ESP_OK && reply[0]==0 && executions==1);
  // Timeout recovery is a HELLO, never a replay of that command.
  s_transport_ready=false;
  assert(transport_send(frame,clock_us+100000)==ESP_OK);
  assert(executions==2 && syncs==2);transport_read(reply,sizeof(reply));
  // Reusing a sequence with changed request bytes is rejected.
  spi_transport_header_t h;memcpy(saved,peer_cache,sizeof(saved));
  assert(spi_transport_valid(saved,sizeof(saved),&h));
  saved[SPI_TRANSPORT_HEADER_SIZE+3]^=1;
  spi_transport_seal(saved,SPI_FRAME_SIZE,SPI_TRANSPORT_REQUEST,h.master_session,h.slave_session,h.sequence);
  assert(transport_classify(saved,peer_master,peer_boot,peer_seq,peer_cache,peer_cached_size,&h)==SPI_TRANSPORT_DROP);
  // Restarting the C5 rejects the old session before any side effects.
  peer_boot++;peer_master=0;peer_seq=0;peer_cached_size=0;pending=false;
  assert(transport_send(frame,clock_us+100000)==ESP_OK);transport_read(reply,sizeof(reply));
  assert(!reply[0] && executions==2);
  s_transport_ready=false;assert(transport_send(frame,clock_us+100000)==ESP_OK);
  transport_read(reply,sizeof(reply));assert(reply[0]==SPI_SYNC_BYTE && executions==3);
  // P4 restart and sequence exhaustion both negotiate new sessions.
  uint64_t old=s_master_session;s_master_session=0;s_transport_ready=false;
  assert(transport_send(frame,clock_us+100000)==ESP_OK && s_master_session!=old);
  transport_read(reply,sizeof(reply));old=s_master_session;s_transaction=UINT32_MAX;
  assert(transport_send(frame,clock_us+100000)==ESP_OK && s_master_session!=old);
  transport_read(reply,sizeof(reply));
  // Recovery notification pauses the master without extending a caller deadline.
  assert(transport_send(frame,clock_us+100000)==ESP_OK);
  recover_reply=true;
  assert(transport_read(reply,sizeof(reply))==ESP_ERR_INVALID_STATE && !s_transport_ready);
  int64_t quiet_start=clock_us;
  assert(transport_send(frame,clock_us+10000)==ESP_ERR_TIMEOUT);
  assert(clock_us==quiet_start);
  assert(transport_send(frame,clock_us+100000)==ESP_OK);
  assert(clock_us>=quiet_start+SPI_TRANSPORT_RECOVERY_PAUSE_MS*1000);
  transport_read(reply,sizeof(reply));
  // Corrupt and truncated packets cannot pass CRC/bounds validation.
  memcpy(saved,peer_cache,sizeof(saved));
  for(size_t i=0;i<sizeof(saved);i++) {
    saved[i]^=1;assert(!spi_transport_valid(saved,sizeof(saved),&h));saved[i]^=1;
  }
  for(size_t i=0;i<sizeof(saved);i++)assert(!spi_transport_valid(saved,i,&h));
  corrupt_reply=true;assert(transport_send(frame,clock_us+100000)==ESP_OK);
  unsigned once=executions;
  transport_read(reply,sizeof(reply));assert(!reply[0]);
  transport_read(reply,sizeof(reply));assert(!reply[0]);
  transport_read(reply,sizeof(reply));assert(reply[0]==SPI_SYNC_BYTE && executions==once);
  // A CRC-valid reply for the previous transaction is rejected during RX.
  stale_reply=true;assert(transport_send(frame,clock_us+100000)==ESP_OK);once=executions;
  transport_read(reply,sizeof(reply));assert(!reply[0]);
  transport_read(reply,sizeof(reply));assert(!reply[0]);
  transport_read(reply,sizeof(reply));assert(reply[0]==SPI_SYNC_BYTE && executions==once);
  // Losing command delivery does not turn FETCH into a command retry.
  drop_request=true;assert(transport_send(frame,clock_us+100000)==ESP_OK);once=executions;
  for(unsigned i=0;i<5;i++){transport_read(reply,sizeof(reply));assert(!reply[0]);}
  assert(executions==once);s_transport_ready=false;
  assert(transport_send(frame,clock_us+100000)==ESP_OK);transport_read(reply,sizeof(reply));
  assert(executions==once+1);
  // An older HELLO cannot rewind this master's duplicate protection.
  memcpy(saved,peer_cache,sizeof(saved));
  spi_transport_seal(saved,SPI_FRAME_SIZE,SPI_TRANSPORT_HELLO,peer_master,0,peer_seq-1);
  assert(transport_classify(saved,peer_master,peer_boot,peer_seq,peer_cache,peer_cached_size,&h)==SPI_TRANSPORT_DROP);
  // A sync can drain a pending response, including a truncated stream frame.
  peer_cached_size=SPI_STREAM_FRAME_SIZE;pending=true;s_transport_ready=false;
  assert(transport_send(frame,clock_us+100000)==ESP_OK);transport_read(reply,sizeof(reply));
  suppress_peer=true;s_transport_ready=false;unsigned before=commands_sent;
  int64_t deadline=clock_us+10000;
  assert(transport_send(frame,deadline)==ESP_ERR_TIMEOUT);
  assert(clock_us<deadline+3000 && commands_sent==before);
  assert(transport_send(frame,clock_us)==ESP_ERR_TIMEOUT);
  puts("PASS: actual SPI codec/master helpers/slave admission; late replies, duplicates, restarts, wrap, CRC, bounds and bounded resync");
  return 0;
}
