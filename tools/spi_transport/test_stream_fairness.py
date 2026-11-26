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

"""Check production stream-task admission during continuous Ethernet commands."""
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
src=(ROOT/'firmware_p4/components/Service/spi_bridge/spi_bridge.c').read_text()
a=src.index('static void stream_task(void *arg) {')
function=src[a:src.index('\nstatic esp_err_t',a)]
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <setjmp.h>
#define pdTRUE 1
#define ESP_OK 0
#define SPI_STREAM_IDLE_MS 20
#define SPI_STREAM_POLL_MS 10
#define SPI_STREAM_YIELD_MS 1
#define SPI_MUTEX_TIMEOUT_MS 50
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t;
typedef void(*spi_stream_cb_t)(uint16_t,const uint8_t*,uint8_t);
static jmp_buf stop;
static atomic_uint s_stream_epoch;
static int s_spi_mutex=1;
static bool has_callbacks=true,s_is_command_in_flight=true,deny_lock;
static unsigned waited,fetched,released,delays;
static bool has_any_stream_cb(void){return has_callbacks;}
static int xSemaphoreTake(int lock,int wait){
 assert(lock==s_spi_mutex&&wait==SPI_MUTEX_TIMEOUT_MS);waited++;
 if(deny_lock)return 0;
 // A queued waiter runs when the current owner releases the mutex.
 s_is_command_in_flight=false;return pdTRUE;
}
static void xSemaphoreGive(int lock){assert(lock==s_spi_mutex);released++;s_is_command_in_flight=true;}
static uint32_t spi_bridge_link_generation(void){return 1;}
static esp_err_t fetch_stream(const uint8_t **records,uint16_t *len){
 assert(!s_is_command_in_flight);fetched++;*records=NULL;*len=0;return ESP_OK;
}
static spi_stream_cb_t get_stream_cb(uint16_t op){(void)op;return NULL;}
static void vTaskDelay(int ms){assert(ms==20||ms==10||ms==1);if(++delays==4)longjmp(stop,1);}
"""
tests=r"""
int main(void){
 if(!setjmp(stop))stream_task(NULL);
 assert(waited==4&&fetched==4&&released==4);
 deny_lock=true;waited=fetched=released=delays=0;
 if(!setjmp(stop))stream_task(NULL);
 assert(waited==4&&fetched==0&&released==0);
 has_callbacks=false;waited=delays=0;
 if(!setjmp(stop))stream_task(NULL);
 assert(waited==0);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='spi-fairness-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture+function+tests)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                 '-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=10)
print('PASS: stream polling queues behind active packets; idle callbacks and lock failures remain bounded')
