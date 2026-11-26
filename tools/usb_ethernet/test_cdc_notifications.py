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

"""Exercise patched production CDC notifications and source integrity guard."""
import subprocess
import tempfile
from pathlib import Path
from prepare_cdc import prepare

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware_p4/managed_components/espressif__usb_host_cdc_acm/cdc_acm_host.c"
FIXTURE = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#define USB_CDC_NOTIF_NETWORK_CONNECTION 0
#define USB_CDC_NOTIF_SERIAL_STATE 0x20
#define USB_CDC_NOTIF_RESPONSE_AVAILABLE 1
#define USB_CDC_NOTIF_CONNECTION_SPEED_CHANGE 0x2a
#define CDC_ACM_HOST_NETWORK_CONNECTION 1
#define CDC_ACM_HOST_SERIAL_STATE 2
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) (++warnings)
#define ESP_LOG_BUFFER_HEX(...) (++dumps)
static unsigned warnings,dumps,submitted,events;
typedef union {uint16_t val;} serial_t;
typedef struct {int type; union {bool network_connected;serial_t serial_state;} data;} cdc_acm_host_dev_event_data_t;
typedef struct {void *context;uint8_t *data_buffer;size_t actual_num_bytes;} usb_transfer_t;
typedef struct {uint8_t bmRequestType,bNotificationCode;uint16_t wValue,wIndex,wLength;uint8_t Data[];} __attribute__((packed)) cdc_notification_t;
typedef struct {struct {void (*cb)(const cdc_acm_host_dev_event_data_t *,void *);void *xfer;} notif;serial_t serial_state;void *cb_arg;} cdc_dev_t;
static bool cdc_acm_is_transfer_completed(usb_transfer_t *t){(void)t;return true;}
static void usb_host_transfer_submit(void *p){(void)p;submitted++;}
static void notify(const cdc_acm_host_dev_event_data_t *e,void *p){(void)p;assert(e->type==CDC_ACM_HOST_NETWORK_CONNECTION&&e->data.network_connected);events++;}
/* PRODUCTION */
int main(void){
 uint8_t data[16]={0};cdc_notification_t *n=(void*)data;
 cdc_dev_t dev={.notif={.cb=notify}};
 usb_transfer_t transfer={.context=&dev,.data_buffer=data,.actual_num_bytes=sizeof(data)};
 n->bNotificationCode=USB_CDC_NOTIF_CONNECTION_SPEED_CHANGE;n->wLength=8;
 for(unsigned i=0;i<10000;i++)notif_xfer_cb(&transfer);
 assert(submitted==10000 && warnings==0 && dumps==0 && events==0);
 n->wLength=7;notif_xfer_cb(&transfer);assert(warnings==1);
 n->wLength=8;transfer.actual_num_bytes=15;notif_xfer_cb(&transfer);assert(warnings==2);
 transfer.actual_num_bytes=1;notif_xfer_cb(&transfer);assert(warnings==3);
 transfer.actual_num_bytes=16;n->bNotificationCode=0xff;notif_xfer_cb(&transfer);assert(warnings==4&&dumps==1);
 n->bNotificationCode=USB_CDC_NOTIF_NETWORK_CONNECTION;n->wValue=1;notif_xfer_cb(&transfer);assert(events==1&&warnings==4);
 assert(submitted==10005);
}
"""
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    patched = path / "cdc_acm_host.c"
    prepare(SOURCE, patched)
    source = patched.read_text()
    start = source.index("#define HIGHBOY_NCM_SPEED_BYTES")
    end = source.index("static void out_xfer_cb", start)
    fixture = SOURCE.read_text().split("#include", 1)[0] + FIXTURE.replace("/* PRODUCTION */", source[start:end])
    (path / "test.c").write_text(fixture)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
    changed = path / "changed.c"
    changed.write_bytes(SOURCE.read_bytes() + b"\n")
    try:
        prepare(changed, path / "must-not-exist.c")
    except SystemExit:
        pass
    else:
        raise AssertionError("Changed dependency was accepted")
print("CDC NCM: 10000 speed notifications without flood; malformed/unknown warnings and network callback preserved")
