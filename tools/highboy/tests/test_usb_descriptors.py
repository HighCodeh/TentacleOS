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

"""Validate production FS/HS USB descriptor layouts against TinyUSB macros."""
from pathlib import Path
import subprocess
import tempfile
import os

ROOT = Path(__file__).resolve().parents[3]
NOTICE = (ROOT / "firmware_p4/components/Service/fatfs/patch_manifest.json.license").read_text()
C_NOTICE = "\n".join("// " + line for line in NOTICE.splitlines()) + "\n"
source = (ROOT / "firmware_p4/components/Drivers/tusb_desc/tusb_desc.c").read_text()
lifecycle = source[source.index("esp_err_t busb_set_hid_exposed"):source.index("bool busb_msc_is_exposed")]
source = source[source.index("// USB HID Report IDs"):source.index("const uint8_t *tud_hid_descriptor_report_cb")]
header = (ROOT / "firmware_p4/components/Drivers/tusb_desc/include/tusb_desc.h").read_text()
header = header[header.index("// Composite device interfaces"):header.index("/**")]
with tempfile.TemporaryDirectory(prefix="hb-usb-desc-") as temp:
    work = Path(temp)
    (work / "tusb_config.h").write_text(C_NOTICE + """
#define CFG_TUSB_MCU OPT_MCU_NONE
#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)
#define TUP_DCD_ENDPOINT_MAX 8
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 1
#define CFG_TUD_MSC_EP_BUFSIZE 1024
#define CFG_TUD_HID 1
#define CFG_TUD_HID_EP_BUFSIZE 16
#define HB_GROK_USB_AUDIO_ENABLED 0
""")
    test = C_NOTICE + '#include <assert.h>\n#include <stdatomic.h>\n#include <string.h>\n#include "tusb.h"\n' + header + """
static tusb_speed_t speed;
tusb_speed_t tud_speed_get(void) { return speed; }
""" + source + r"""
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define pdMS_TO_TICKS(ms) (ms)
static bool s_installed;
static int disconnects, connects;
bool tud_disconnect(void) { disconnects++; return true; }
bool tud_connect(void) { connects++; return true; }
static void vTaskDelay(int ticks) { assert(ticks == BUSB_REENUM_DELAY_MS); }
bool busb_voice_is_exposed(void) { return false; }
""" + lifecycle + r"""
static void check(const uint8_t *data, int type, int interfaces, int bulk_size, int hid) {
  const tusb_desc_configuration_t *cfg=(const tusb_desc_configuration_t *)data;
  assert(cfg->bDescriptorType==type && cfg->bNumInterfaces==interfaces);
  unsigned offset=0, seen=0, eps=0, hids=0; bool endpoints[256]={0};
  while(offset<cfg->wTotalLength) {
    assert(data[offset]>=2 && offset+data[offset]<=cfg->wTotalLength);
    if(data[offset+1]==TUSB_DESC_INTERFACE) { seen++; if(data[offset+5]==TUSB_CLASS_HID) hids++; }
    if(data[offset+1]==TUSB_DESC_ENDPOINT) {
      const tusb_desc_endpoint_t *ep=(const tusb_desc_endpoint_t *)(data+offset);
      assert(!endpoints[ep->bEndpointAddress]); endpoints[ep->bEndpointAddress]=true; eps++;
      if(ep->bmAttributes.xfer==TUSB_XFER_BULK) assert(ep->wMaxPacketSize==bulk_size);
    }
    offset+=data[offset];
  }
  assert(offset==cfg->wTotalLength && seen==(unsigned)interfaces);
  assert(hids==(unsigned)hid);
  assert(eps==(interfaces==2 ? 3u : hid ? 4u : 5u));
}
int main(void) {
  for(int mode=0;mode<3;mode++) {
    s_msc_exposed=mode==2;
    s_hid_exposed=mode==1;
    for(int hs=0;hs<2;hs++) {
      speed=hs ? TUSB_SPEED_HIGH:TUSB_SPEED_FULL;
      check(tud_descriptor_configuration_cb(0),TUSB_DESC_CONFIGURATION,mode==0?2:3,hs?512:64,mode==1);
      check(tud_descriptor_other_speed_configuration_cb(0),TUSB_DESC_OTHER_SPEED_CONFIG,mode==0?2:3,hs?64:512,mode==1);
      const tusb_desc_device_t *dev=(const tusb_desc_device_t *)tud_descriptor_device_cb();
      assert(dev->idProduct==(mode==2?0x4005:mode==1?0x4001:0x4004));
      const tusb_desc_device_qualifier_t *q=(const tusb_desc_device_qualifier_t *)tud_descriptor_device_qualifier_cb();
      assert(q->bDeviceClass==dev->bDeviceClass && q->bNumConfigurations==1 && q->bLength==10);
    }
  }
  s_msc_exposed=false; s_hid_exposed=false;
  assert(busb_set_hid_exposed(true)==ESP_OK && s_hid_exposed);
  assert(connects==0 && disconnects==0);
  s_installed=true;
  assert(busb_set_hid_exposed(false)==ESP_OK && !s_hid_exposed);
  assert(connects==1 && disconnects==1);
  assert(busb_set_hid_exposed(true)==ESP_OK);
  busb_set_msc_exposed(true);
  assert(s_msc_exposed && !s_hid_exposed);
  assert(busb_set_hid_exposed(true)==ESP_ERR_INVALID_STATE);
  busb_set_msc_exposed(false);
  assert(!s_msc_exposed && !s_hid_exposed);
  assert(connects==4 && disconnects==4);
  assert(!tud_descriptor_configuration_cb(1) && !tud_descriptor_other_speed_configuration_cb(1));
  return 0;
}
"""
    (work / "test.c").write_text(test)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-fsanitize=address,undefined",
                    "-I", str(work), "-I", str(ROOT / "firmware_p4/managed_components/espressif__tinyusb/src"),
                    str(work / "test.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
print("PASS: USB FS/HS, MSC/base, Other Speed, qualifier, endpoints and product IDs")
