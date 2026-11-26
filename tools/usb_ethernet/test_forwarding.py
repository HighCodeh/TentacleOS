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

"""Test control-command BUSY retries and bounded packet-exchange timeouts."""
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
src=(ROOT/"firmware_p4/components/Service/ethernet_share/ethernet_share.c").read_text()
start=src.index("static esp_err_t command(")
function=src[start:src.index("\nstatic void worker",start)]
fixture=r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
typedef int esp_err_t;
typedef int spi_id_t;
typedef struct {int type,cmd;size_t length;} spi_header_t;
#define SPI_TYPE_RESP 2
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NOT_FINISHED 2
#define ESP_ERR_TIMEOUT 3
#define SPI_ID_ETHERNET_RX 1
#define SPI_ID_ETHERNET_TX 2
#define SPI_ID_ETHERNET_START 3
#define SPI_ID_ETHERNET_STOP 4
#define SPI_TIMEOUT_MS 500
#define TRANSPORT_YIELD_MS 1
#define POLL_MS 20
#define BUSY_RETRY_COUNT 10
#define RX_TIMEOUT_LIMIT 3
#define SPI_ETHERNET_CHUNK 240
typedef struct {uint32_t token;uint16_t sequence,offset,total;} spi_ethernet_fragment_t;
#define pdMS_TO_TICKS(x) (x)
static atomic_bool s_stop;
static unsigned calls,delays,busy_count,yields;
static bool local_failure,wrong_header;
static unsigned timeout_call, rx_calls;
static uint16_t last_offset,last_sequence;
static void vTaskDelay(int ticks){if(ticks==1){yields++;return;}assert(ticks==20);delays++;}
static int spi_header_cmd(const spi_header_t *h){return h->cmd;}
static esp_err_t spi_bridge_send_command(spi_id_t id,const void *data,size_t length,
 spi_header_t *header,void *out,size_t capacity,int timeout){
 (void)data;(void)length;(void)out;(void)capacity;assert(timeout==500);calls++;
 if(id==SPI_ID_ETHERNET_RX && data){
  spi_ethernet_fragment_t f;memcpy(&f,data,sizeof(f));
  rx_calls++;last_offset=f.offset;last_sequence=f.sequence;
 }
 if(calls==timeout_call)return ESP_ERR_TIMEOUT;
 if(local_failure)return ESP_ERR_INVALID_STATE;
 header->type=SPI_TYPE_RESP;header->cmd=wrong_header?id+1:id;
 if(calls<=busy_count)return ESP_ERR_INVALID_STATE;
 header->length=8;return ESP_OK;
}
"""
tests=r"""
int main(void){
 size_t count=99;busy_count=2;
 assert(command(SPI_ID_ETHERNET_TX,NULL,0,NULL,0,&count)==ESP_OK);
 assert(calls==3&&delays==2&&count==8);
 calls=delays=0;count=99;busy_count=99;
 assert(command(SPI_ID_ETHERNET_TX,NULL,0,NULL,0,&count)==ESP_ERR_TIMEOUT);
 assert(calls==10&&delays==10&&count==0);
 calls=delays=0;
 assert(command(SPI_ID_ETHERNET_RX,NULL,0,NULL,0,NULL)==ESP_ERR_NOT_FINISHED);
 assert(calls==1&&delays==0);
 calls=0;assert(command(SPI_ID_ETHERNET_START,NULL,0,NULL,0,NULL)==ESP_ERR_NOT_FINISHED);
 assert(calls==1);
 calls=0;local_failure=true;
 assert(command(SPI_ID_ETHERNET_TX,NULL,0,NULL,0,NULL)==ESP_ERR_INVALID_STATE&&calls==1);
 local_failure=false;wrong_header=true;calls=0;
 assert(command(SPI_ID_ETHERNET_TX,NULL,0,NULL,0,NULL)==ESP_ERR_INVALID_STATE&&calls==1);
 wrong_header=false;s_stop=true;calls=0;
 assert(command(SPI_ID_ETHERNET_TX,NULL,0,NULL,0,NULL)==ESP_ERR_INVALID_STATE&&calls==1);
 calls=0;busy_count=1;
 assert(command(SPI_ID_ETHERNET_STOP,NULL,0,NULL,0,NULL)==ESP_OK&&calls==2);
 unsigned streak=0;
 assert(recover_exchange_timeout(ESP_ERR_TIMEOUT,&streak)&&streak==1);
 assert(recover_exchange_timeout(ESP_ERR_TIMEOUT,&streak)&&streak==2);
 assert(!recover_exchange_timeout(ESP_ERR_TIMEOUT,&streak)&&streak==3);
 assert(!recover_exchange_timeout(ESP_OK,&streak)&&streak==0);
 assert(!recover_exchange_timeout(ESP_ERR_INVALID_STATE,&streak)&&streak==0);
 assert(yields>0);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/"test.c").write_text(fixture+function+tests)
 subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",str(p/"test.c"),"-o",str(p/"test")],check=True)
 subprocess.run([str(p/"test")],check=True)
print("Forwarding BUSY, RX timeout/no-replay, recovery bound and stop tests passed.")
