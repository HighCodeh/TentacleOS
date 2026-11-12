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

"""Exercise production Classic nonce/IRQ functions with injected bus/receive faults."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'firmware_p4/components/Drivers/st25r3916/st25r3916_irq.c').read_text()
function=source[source.index('esp_err_t st25r3916_irq_wait_exchange'):]
fake=r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef int esp_err_t;
typedef struct {uint8_t main,error,timer,target,collision;} st25r3916_irq_status_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_TIMEOUT 2
#define ESP_FAIL 3
#define ST25R3916_IRQ_MAIN_TXE 8
#define ST25R3916_IRQ_MAIN_RXE 16
#define ST25R3916_REG_ERROR_INT 28
#define ST25R3916_FIFO_WAIT_POLL_US 50
static int64_t now;static unsigned calls;static int failed;static uint8_t late;
static st25r3916_irq_status_t events[8];
static int64_t esp_timer_get_time(void){return now;}
static void hb_nfc_timer_delay_us(int us){now+=us;}
static int st25r3916_irq_read(st25r3916_irq_status_t*out){unsigned i=calls++;if((int)i==failed)return ESP_FAIL;*out=events[i<8?i:7];return ESP_OK;}
static int hb_nfc_spi_reg_read(int r,uint8_t*out){assert(r==28);*out=late;return failed==99?ESP_FAIL:ESP_OK;}
static void reset(void){now=0;calls=0;failed=-1;late=0;memset(events,0,sizeof(events));}
"""
test=r"""
int main(void){st25r3916_irq_status_t out;
reset();events[0].main=24;assert(st25r3916_irq_wait_exchange(1,&out)==0&&out.main==24&&calls==1);
reset();events[0].main=8;events[1].main=16;assert(st25r3916_irq_wait_exchange(1,&out)==0&&calls==2);
reset();events[0].main=16;events[1].main=8;assert(st25r3916_irq_wait_exchange(1,&out)==0&&out.main==24);
reset();events[0].main=8;events[0].error=16;events[0].timer=2;events[1].main=16;events[1].target=3;late=128;
assert(st25r3916_irq_wait_exchange(1,&out)==0&&out.error==144&&out.timer==2&&out.target==3);
reset();events[0].main=8;assert(st25r3916_irq_wait_exchange(1,&out)==ESP_ERR_TIMEOUT&&out.main==8&&now==1000);
reset();events[0].main=24;failed=99;assert(st25r3916_irq_wait_exchange(1,&out)==ESP_FAIL);
reset();failed=0;assert(st25r3916_irq_wait_exchange(1,&out)==ESP_FAIL);
assert(st25r3916_irq_wait_exchange(0,&out)==ESP_ERR_INVALID_ARG);
assert(st25r3916_irq_wait_exchange(1,NULL)==ESP_ERR_INVALID_ARG);
return 0;}
"""
with tempfile.TemporaryDirectory() as d:
 exe=str(Path(d)/'irq')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-x','c','-','-o',exe],input=fake+function+test,text=True,check=True)
 subprocess.run([exe],check=True)
print('PASS: actual IRQ exchange preserves simultaneous/split/reversed TXE+RXE, late errors, timeout state and SPI faults.')
