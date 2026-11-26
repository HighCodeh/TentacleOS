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

"""Check NCM data alternate discovery and setup requests against production C."""
import re
import subprocess
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
src = (ROOT / "firmware_p4/components/Drivers/usb_ethernet/usb_ethernet.c").read_text()
def extract(name):
    m = re.search(r"static\s+\w+\s+" + name + r"\([^;{]*\{.*?\n}", src, re.S)
    assert m, name
    return m.group()
defines = "\n".join(line for line in src.splitlines() if line.startswith("#define "))
fixture = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define ESP_LOGI(...) ((void)0)
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED 1
#define ESP_ERR_INVALID_RESPONSE 2
#define ESP_ERR_TIMEOUT 3
#define USB_ETHERNET_NTB_SIZE 2048
#define USB_ETHERNET_FRAME_MIN 14
#define USB_ETHERNET_FRAME_MAX 1514
typedef int esp_err_t;
typedef struct __attribute__((packed)) {uint8_t bLength,type;uint16_t wTotalLength;uint8_t rest[5];} usb_config_desc_t;
typedef struct {uint8_t bytes[9];} usb_intf_desc_t;
typedef struct {uint16_t vid,pid;uint8_t address,interface,mac_string,capabilities,data_interface,data_alternate;} usb_ethernet_device_t;
static usb_ethernet_device_t s_descriptor;
static void *s_device;
static uint16_t s_divisor,s_remainder,s_alignment;
static size_t s_tx_limit;
static uint8_t s_mac[6];
static unsigned calls;
static bool fail_set,wrong_get;
static uint16_t le16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t le32(const uint8_t *p){return le16(p)|((uint32_t)le16(p+2)<<16);}
static esp_err_t cdc_acm_host_send_custom_request(void *dev,uint8_t dir,uint8_t req,
 uint16_t value,uint16_t index,uint16_t len,uint8_t *data){
 (void)dev;calls++;
 switch(req){
 case GET_PARAMETERS:
  assert(dir==CONTROL_IN&&index==4&&len==28);memset(data,0,len);
  data[0]=28;data[2]=1;data[5]=8;data[17]=8;data[20]=4;data[24]=4;break;
 case USB_STRING_REQUEST:
  assert(dir==0x80&&len==26);memset(data,0,len);data[0]=26;data[1]=3;
  for(unsigned i=0;i<12;i++)data[2+i*2]='0';data[4]='2';break;
 case SET_INPUT_SIZE: assert(dir==CONTROL_OUT&&len==4);break;
 case SET_INTERFACE:
  assert(dir==STANDARD_INTERFACE_OUT&&value==1&&index==5&&len==0);
  if(fail_set)return ESP_ERR_TIMEOUT;break;
 case GET_INTERFACE:
  assert(dir==STANDARD_INTERFACE_IN&&index==5&&len==1);
  *data=wrong_get?0:1;break;
 case SET_FILTER:assert(calls==6&&value==FILTER_DIRECTED_BROADCAST);break;
 default:assert(false);
 }
 return ESP_OK;
}
"""
tests = r"""
int main(void){
 uint8_t bytes[]={9,2,36,0,2,2,0,0,0,
  9,4,4,0,1,2,13,0,0,
  9,4,5,0,0,10,0,0,0,
  9,4,5,1,2,10,0,0,0};
 s_descriptor.interface=4;
 assert(data_setting((void*)bytes,&s_descriptor));
 assert(s_descriptor.data_interface==5&&s_descriptor.data_alternate==1);
 bytes[29]=6;assert(!data_setting((void*)bytes,&s_descriptor));bytes[29]=5;
 bytes[27]=0;assert(!data_setting((void*)bytes,&s_descriptor));bytes[27]=9;
 bytes[2]=34;assert(!data_setting((void*)bytes,&s_descriptor));
 s_descriptor.mac_string=3;
 assert(configure()==ESP_OK&&calls==6);
 calls=0;fail_set=true;assert(configure()==ESP_ERR_TIMEOUT&&calls==4);
 calls=0;fail_set=false;wrong_get=true;assert(configure()==ESP_ERR_INVALID_RESPONSE&&calls==5);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / "test.c").write_text(defines + "\n" + fixture + extract("data_setting") +
                            extract("control") + extract("hex") + extract("configure") + tests)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    str(p / "test.c"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
print("NCM alternate setting, request ordering and failure tests passed.")
