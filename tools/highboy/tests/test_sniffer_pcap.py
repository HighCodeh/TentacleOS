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

"""Inject PCAP open, write, flush and close failures into production functions."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
s=(ROOT/"firmware_p4/components/Applications/wifi/wifi_sniffer.c").read_text()
def function(name):
    match=list(re.finditer(r"(?:static )?(?:void|bool|esp_err_t|size_t) " + name + r"\([^;]+?\) \{",s,re.S))[-1]
    end=match.end();depth=1
    while depth:
        depth += (s[end]=="{")-(s[end]=="}")
        end+=1
    return s[match.start():end]
fakes=r"""
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include <sys/time.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define portMAX_DELAY 0
#define WIFI_SNIFFER_PATH_MAX 256
#define WIFI_SNIFFER_PCAP_SNAPLEN 65535
#define PCAP_MAGIC_NUMBER 0xa1b2c3d4
#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4
#define PCAP_LINK_TYPE_802_11_RADIOTAP 127
#define RADIOTAP_PRESENT_FLAGS 2
#define RADIOTAP_PRESENT_CHANNEL 8
#define RADIOTAP_PRESENT_DBM 32
#define RADIOTAP_F_FCS 16
#define RADIOTAP_CHAN_2GHZ 128
#define RADIOTAP_CHAN_5GHZ 256
struct stream {size_t bytes;};
typedef struct stream *storage_stream_t;
static struct stream stream;
static storage_stream_t s_capture_stream;
static int s_capture_mutex_storage;
static void *s_capture_mutex;
static esp_err_t s_capture_error;
static atomic_uint s_session_id=1;
static uint32_t spi_session_callback_id(void) {return 1;}
static atomic_bool s_is_ap_capture;
static int wifi_capture_queue_push(const uint8_t *p,size_t n) {(void)p;(void)n;return ESP_OK;}
static int wifi_capture_queue_stop(void) {return 0;}
static void wifi_ap_capture_stop(void) {}
static size_t wifi_ap_capture_bytes(void) {return 0;}
static char s_capture_path[256];
typedef void (*wifi_sniffer_cb_t)(const uint8_t *,uint16_t,int8_t,uint8_t);
static _Atomic(wifi_sniffer_cb_t) s_stream_cb;
static int fail_write, writes, flush_error,close_error,closes;
static bool fail_open;
static void *xSemaphoreCreateMutexStatic(void *p) { return p; }
static void xSemaphoreTake(void *p,int t) {(void)t;assert(p);}
static void xSemaphoreGive(void *p) {assert(p);}
static void ensure_parent_dir(const char *p) {(void)p;}
static uint16_t channel_to_freq(uint8_t ch) {return 2407+ch*5;}
static storage_stream_t storage_stream_open(const char *p,const char *mode) {
 (void)p;(void)mode; stream.bytes=0;return fail_open?NULL:&stream;
}
static int storage_stream_write(storage_stream_t p,const void *data,size_t len) {
 assert(p);assert(data);writes++; if(writes==fail_write)return ESP_FAIL;p->bytes+=len;return ESP_OK;
}
static int storage_stream_flush(storage_stream_t p) {assert(p);return flush_error;}
static int storage_stream_close(storage_stream_t p) {assert(p);closes++;return close_error;}
static size_t storage_stream_bytes_written(storage_stream_t p) {return p?p->bytes:0;}
typedef struct {uint32_t magic_number;uint16_t version_major,version_minor;uint32_t thiszone,sigfigs,snaplen,network;} pcap_global_header_t;
typedef struct {uint32_t ts_sec,ts_usec,incl_len,orig_len;} pcap_packet_header_t;
"""
a=s.index("typedef struct {");b=s.index("radiotap_header_t;",a)+len("radiotap_header_t;")
tests=r"""
int main(void) {
 fail_open=true;assert(!wifi_sniffer_start_capture("test.pcap"));assert(!s_capture_stream);
 fail_open=false;fail_write=1;assert(!wifi_sniffer_start_capture("test.pcap"));assert(!s_capture_stream && closes==1);
 fail_write=0;flush_error=ESP_FAIL;assert(!wifi_sniffer_start_capture("test.pcap"));assert(!s_capture_stream);
 flush_error=0;assert(wifi_sniffer_start_capture("test.pcap"));assert(wifi_sniffer_get_capture_size()==24);
 assert(!wifi_sniffer_start_capture("other.pcap"));
 uint8_t frame[24]={0};emit_frame(frame,sizeof(frame),-50,1);assert(wifi_sniffer_flush_capture()==0);
 s_session_id=2;int old_writes=writes;emit_frame(frame,sizeof(frame),-50,1);assert(writes==old_writes);s_session_id=1;
 fail_write=writes+2;emit_frame(frame,sizeof(frame),-50,1);
 assert(wifi_sniffer_flush_capture()==ESP_FAIL);int count=writes;
 emit_frame(frame,sizeof(frame),-50,1);assert(writes==count);
 assert(!wifi_sniffer_stop_capture());assert(!s_capture_stream);
 assert(wifi_sniffer_start_capture("test.pcap"));close_error=ESP_FAIL;
 assert(!wifi_sniffer_stop_capture());assert(!s_capture_stream);
 puts("PASS: open/header/record/flush/close errors, latched failure and no overwrite");
}
"""
with tempfile.TemporaryDirectory(prefix="sniffer-pcap-") as tmp:
    path=Path(tmp);c=path/"test.c"
    c.write_text(fakes+s[a:b]+"\n"+"\n".join(function(n) for n in ["emit_frame","wifi_sniffer_start_capture","wifi_sniffer_flush_capture","wifi_sniffer_stop_capture","wifi_sniffer_get_capture_size"])+tests)
    binary=path/"test"
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-sanitize-recover=all",str(c),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30)
