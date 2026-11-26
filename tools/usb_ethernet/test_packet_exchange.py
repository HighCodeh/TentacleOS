#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Exercise production Ethernet packet exchange and reply validation under sanitizers."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
master = (ROOT / "firmware_p4/components/Service/spi_bridge/spi_bridge.c").read_text()
slave = (ROOT / "firmware_c5/components/Service/ethernet_router/ethernet_router.c").read_text()
header = (ROOT / "firmware_p4/components/Service/spi_bridge/include/spi_bridge.h").read_text()
exchange_type = re.search(r"typedef struct \{\n  uint32_t token;.*?} spi_bridge_ethernet_exchange_t;", header, re.S).group()
router = slave[slave.index("spi_status_t ethernet_router_exchange("):slave.index("spi_status_t ethernet_router_execute(")]
decoder = master[master.index("static esp_err_t decode_ethernet_reply("):master.index("esp_err_t spi_bridge_lock(uint32_t")]
status = master[master.index("static esp_err_t status_to_err("):master.index("// Public functions")]
fixture = r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include "spi_protocol.h"
#define EXT_RAM_BSS_ATTR
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_RESPONSE -3
#define ESP_ERR_INVALID_STATE -4
#define ESP_ERR_NOT_FOUND -5
#define ESP_ERR_NOT_SUPPORTED -6
#define ESP_ERR_TIMEOUT -7
#define ESP_ERR_NOT_FINISHED -8
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t;
typedef int spi_bridge_phase_t;
enum {SPI_BRIDGE_PHASE_LOCAL,SPI_BRIDGE_PHASE_LOCK,SPI_BRIDGE_PHASE_SEND,
      SPI_BRIDGE_PHASE_RECEIVE,SPI_BRIDGE_PHASE_VALIDATE};
typedef struct {uint16_t length;uint8_t bytes[SPI_ETHERNET_MTU];} ethernet_router_packet_t;
static atomic_bool s_active,s_stop;
static void *s_lock=(void*)1,*s_spi_mutex=(void*)2;
static int s_rx=3,s_tx=4;
static ethernet_router_packet_t rx_packet,tx_packet,queued_rx,queued_tx;
static ethernet_router_packet_t *s_rx_packet=&rx_packet,*s_tx_packet=&tx_packet;
static uint16_t s_rx_offset,s_tx_offset;
static spi_ethernet_start_t s_start;
static spi_ethernet_status_t s_status;
static int64_t s_heartbeat,clock_us;
static bool full_rx,has_tx,router_busy,bridge_busy,bridge_locked,router_locked;
static bool s_suspended,s_bridge_alive=true,s_is_command_in_flight,s_transport_ready=true;
static bool can_recover=true,disable_after_lock;
static unsigned enqueues,dequeues;
static int send_failure,receive_failure;
static unsigned corrupt;
static uint8_t response[SPI_STREAM_FRAME_SIZE];
static int64_t esp_timer_get_time(void){return ++clock_us;}
static int xSemaphoreTake(void *lock,unsigned timeout){
 (void)timeout;assert(lock==s_lock);if(router_busy)return 0;assert(!router_locked);router_locked=true;return pdTRUE;
}
static void xSemaphoreGive(void *lock){
 if(lock==s_lock){assert(router_locked);router_locked=false;}else{assert(lock==s_spi_mutex&&bridge_locked);bridge_locked=false;}
}
static int xQueueSend(int queue,ethernet_router_packet_t *packet,unsigned wait){
 assert(queue==s_rx&&wait==0&&router_locked);if(full_rx)return 0;
 queued_rx=*packet;enqueues++;return pdTRUE;
}
static int xQueueReceive(int queue,ethernet_router_packet_t *packet,unsigned wait){
 assert(queue==s_tx&&wait==0&&router_locked);if(!has_tx)return 0;
 *packet=queued_tx;has_tx=false;dequeues++;return pdTRUE;
}
static esp_err_t retain_failure(spi_id_t id,spi_bridge_phase_t phase,int64_t started,esp_err_t ret){
 (void)id;(void)phase;(void)started;return ret;
}
static bool recovery_allows_command(spi_id_t id){assert(id==SPI_ID_ETHERNET_EXCHANGE);return can_recover;}
static esp_err_t spi_bridge_lock(uint32_t ms){
 assert(ms<=500);if(bridge_busy)return ESP_ERR_TIMEOUT;assert(!bridge_locked);bridge_locked=true;
 if(disable_after_lock)s_suspended=true;return ESP_OK;
}
uint16_t esp_rom_crc16_le(uint16_t crc,const uint8_t *bytes,uint32_t length){
 crc=(uint16_t)~crc;for(uint32_t i=0;i<length;i++){crc^=bytes[i];
 for(unsigned bit=0;bit<8;bit++)crc=(uint16_t)((crc>>1)^((crc&1)?0x8408:0));}return (uint16_t)~crc;
}
/* ROUTER */
static esp_err_t transport_send_frame(const uint8_t *frame,size_t length,int64_t deadline){
 assert(bridge_locked&&s_is_command_in_flight&&length==SPI_STREAM_FRAME_SIZE&&deadline>clock_us);
 if(send_failure)return send_failure;
 const spi_header_t *h=(const spi_header_t *)frame;uint16_t n;
 memcpy(&n,frame+sizeof(*h),2);assert(spi_frame_valid(h,2+n));
 memset(response,0,sizeof(response));uint16_t out_length=0;
 spi_status_t status=ethernet_router_exchange(frame+sizeof(*h)+2,n,response+sizeof(*h)+2,&out_length);
 spi_header_t *reply=(spi_header_t*)response;*reply=*h;
 if(status==SPI_STATUS_OK){reply->type=SPI_TYPE_STREAM;memcpy(response+sizeof(*h),&out_length,2);spi_frame_seal(reply,2+out_length);}
 else{reply->type=SPI_TYPE_RESP;reply->length=1;response[sizeof(*h)]=status;spi_frame_seal(reply,1);}
 return ESP_OK;
}
static esp_err_t recv_frame(uint8_t *tx,uint8_t *rx,size_t length,uint16_t id,bool match,int64_t deadline){
 (void)tx;assert(bridge_locked&&length==sizeof(response)&&id==SPI_ID_ETHERNET_EXCHANGE&&match&&deadline>clock_us);
 if(receive_failure)return receive_failure;
 memcpy(rx,response,length);spi_header_t *h=(spi_header_t*)rx;uint16_t n;memcpy(&n,rx+sizeof(*h),2);
 if(corrupt==1)rx[sizeof(*h)+2]^=1;
 if(corrupt==2){n=0xffff;memcpy(rx+sizeof(*h),&n,2);return ESP_OK;}
 if(corrupt==3)rx[sizeof(*h)+2+4]=SPI_STATUS_ERROR;
 if(corrupt==4)h->op++;
 if(corrupt==5)n=SPI_ETHERNET_REPLY_META_SIZE+1;
 if(corrupt){memcpy(rx+sizeof(*h),&n,2);spi_frame_seal(h,2+n);}
 return ESP_OK;
}
"""
tests = r"""
int main(void){
 uint8_t tx[SPI_ETHERNET_MTU],rx[SPI_ETHERNET_MTU];
 for(unsigned i=0;i<sizeof(tx);i++)tx[i]=(uint8_t)i;
 spi_bridge_ethernet_exchange_t e={.token=123,.tx_data=tx,.tx_length=sizeof(tx),.rx_data=rx,.rx_capacity=sizeof(rx)};
 s_active=true;s_start.token=e.token;s_status.state=SPI_ETHERNET_STATE_SHARING;
 queued_tx.length=sizeof(tx);memcpy(queued_tx.bytes,tx,sizeof(tx));has_tx=true;
 assert(spi_bridge_ethernet_exchange(&e,500)==ESP_OK);
 assert(e.tx_accepted&&e.rx_length==sizeof(tx)&&!memcmp(rx,tx,sizeof(tx)));
 assert(enqueues==1&&dequeues==1&&queued_rx.length==sizeof(tx)&&!memcmp(queued_rx.bytes,tx,sizeof(tx)));
 assert(!bridge_locked&&!router_locked&&!s_is_command_in_flight);
 full_rx=true;has_tx=true;
 assert(spi_bridge_ethernet_exchange(&e,500)==ESP_OK&&!e.tx_accepted&&e.rx_length==sizeof(tx));
 assert(enqueues==1&&dequeues==2&&s_status.dropped==1);
 full_rx=false;e.tx_length=0;
 assert(spi_bridge_ethernet_exchange(&e,500)==ESP_OK&&e.tx_accepted&&e.rx_length==0&&enqueues==1);
 router_busy=true;assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_NOT_FINISHED);
 assert(!bridge_locked&&enqueues==1);router_busy=false;
 e.token++;assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_NOT_FOUND);e.token--;
 s_stop=true;assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_NOT_FOUND);s_stop=false;
 s_tx_offset=1;assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_NOT_FINISHED);s_tx_offset=0;
 for(unsigned i=1;i<6;i++){corrupt=i;assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_INVALID_RESPONSE);
 assert(!e.tx_accepted&&e.rx_length==0&&!bridge_locked);}
 corrupt=0;has_tx=true;e.rx_capacity=14;
 assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_INVALID_RESPONSE&&e.rx_length==0);
 e.rx_capacity=sizeof(rx);e.tx_length=13;
 assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_INVALID_ARG);
 e.tx_length=sizeof(tx)+1;assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_INVALID_ARG);
 e.tx_length=sizeof(tx);unsigned old=enqueues;receive_failure=ESP_ERR_TIMEOUT;
 assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_TIMEOUT&&enqueues==old+1&&!bridge_locked&&!s_transport_ready);
 receive_failure=0;send_failure=ESP_ERR_TIMEOUT;
 assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_TIMEOUT&&enqueues==old+1&&!bridge_locked);
 send_failure=0;bridge_busy=true;
 assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_TIMEOUT&&enqueues==old+1);bridge_busy=false;
 disable_after_lock=true;
 assert(spi_bridge_ethernet_exchange(&e,500)==ESP_ERR_INVALID_STATE&&!bridge_locked);
 disable_after_lock=false;s_suspended=false;
 assert(spi_bridge_ethernet_exchange(NULL,500)==ESP_ERR_INVALID_ARG);
 uint8_t body[SPI_ETHERNET_REQUEST_META_SIZE+SPI_ETHERNET_MTU];uint16_t n=99;
 memcpy(body,&e.token,4);
 assert(ethernet_router_exchange(body,3,response,&n)==SPI_STATUS_INVALID_ARG&&n==0);
 assert(ethernet_router_exchange(body,5,response,&n)==SPI_STATUS_INVALID_ARG);
 assert(ethernet_router_exchange(body,sizeof(body)+1,response,&n)==SPI_STATUS_INVALID_ARG);
 assert(!bridge_locked&&!router_locked&&!s_is_command_in_flight);
 puts("PASS: whole Ethernet packets, duplex congestion, token/CRC/length rejection, deadlines and lock cleanup");
 return 0;
}
"""
source = fixture.replace("/* ROUTER */", exchange_type + router) + status + decoder + tests
with tempfile.TemporaryDirectory(prefix="ethernet-packet-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(source)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-I", str(ROOT / "tools/mqtt/fixtures"),
                    "-I", str(ROOT / "firmware_p4/components/Service/spi_bridge/include"),
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=30)
