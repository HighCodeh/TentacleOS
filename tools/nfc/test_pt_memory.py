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
from pathlib import Path
"""Production PT memory SPI framing, boundaries and failure propagation."""
from pathlib import Path
import re,subprocess,tempfile
from source import function
r=Path(__file__).resolve().parents[2]
s=(r/'firmware_p4/components/Drivers/st25r3916/hal/hb_nfc_spi.c').read_text()
constants="\n".join(re.findall(r"^#define HB_NFC_SPI_[^\n]+",s,re.M))
code="\n".join(function(s,n) for n in ('esp_err_t hb_nfc_spi_pt_mem_write(', 'esp_err_t hb_nfc_spi_pt_mem_read('))
fake=r"""
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ST25R3916_SPI_PT_MEM_A_WRITE 0xA0
#define ST25R3916_SPI_PT_MEM_F_WRITE 0xA8
#define ST25R3916_SPI_PT_MEM_TSN_WRITE 0xAC
typedef struct {uint32_t length; const void*tx_buffer; void*rx_buffer;} spi_transaction_t;
static int fail,calls;static uint8_t sent[64];static size_t count;
static int transmit(spi_transaction_t*t){calls++;count=t->length/8;assert(count<=64);memcpy(sent,t->tx_buffer,count);if(fail)return 9;if(t->rx_buffer){uint8_t*p=t->rx_buffer;for(size_t i=0;i<count;i++)p[i]=(uint8_t)i;}return 0;}
"""
tests=r"""
int main(void){
uint8_t data[50];for(int i=0;i<50;i++)data[i]=(uint8_t)(0x80+i);
assert(hb_nfc_spi_pt_mem_write(0xA8,data,21)==ESP_OK);assert(count==22&&sent[0]==0xA8&&memcmp(sent+1,data,21)==0);
for(int kind=0;kind<3;kind++){const int sizes[]={15,21,12},prefixes[]={0xA0,0xA8,0xAC};for(int n=0;n<=50;n++){calls=0;int e=hb_nfc_spi_pt_mem_write(prefixes[kind],data,n);assert(e==((n>0&&n<=sizes[kind])?0:1));assert(calls==(e==0));}}
assert(hb_nfc_spi_pt_mem_write(0xAA,data,5)==1);assert(hb_nfc_spi_pt_mem_write(0xA8,NULL,21)==1);
for(int n=0;n<=50;n++){calls=0;memset(data,0xAA,sizeof data);int e=hb_nfc_spi_pt_mem_read(data+1,n);assert(e==((n>0&&n<=48)?0:1));assert(data[0]==0xAA&&data[49]==0xAA);if(e==0){assert(count==(size_t)n+2&&sent[0]==0xBF);for(int i=0;i<n;i++)assert(data[i+1]==i+2);}}
fail=1;memset(data,0xAA,sizeof data);assert(hb_nfc_spi_pt_mem_write(0xA8,data,21)==9);assert(hb_nfc_spi_pt_mem_read(data,36)==9);for(int i=0;i<50;i++)assert(data[i]==0xAA);
return 0;}
"""
with tempfile.TemporaryDirectory() as d:
 exe=str(Path(d)/'pt')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined,bounds','-x','c','-','-o',exe],input=fake+constants+"\n"+code+tests,text=True,check=True)
 subprocess.run([exe],check=True,timeout=20)
print('PASS: production PT SPI; 21-byte NFC-F regression, all section boundaries, read framing/canaries, write/read failures.')
