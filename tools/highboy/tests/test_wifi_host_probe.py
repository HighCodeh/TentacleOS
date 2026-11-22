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

"""Exercise the C5 ICMP probe using fake sockets; no network traffic."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
WIFI = ROOT / "firmware_c5/components/Service/wifi"
NOTICE = (WIFI / "wifi_host_probe.c").read_text().split("#include", 1)[0]
CODE = r"""
#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "wifi_host_probe.h"
static int64_t now;
static unsigned sends, receives, closes, mode, sockets, delivered;
static uint8_t sent[16];
static struct sockaddr_in destination;
static unsigned host_sends[8];
static int64_t last_send[8];
typedef struct { uint8_t echo[16]; struct sockaddr_in source; int64_t ready; bool used; } reply_t;
static reply_t replies[64];
static unsigned reply_count;
static int64_t esp_timer_get_time(void) { return now; }
static uint32_t esp_random(void) { return 0x12763489; }
static uint16_t inet_chksum(const void *data, uint16_t length) {
  const uint8_t *p=data; uint32_t sum=0;
  for(unsigned i=0;i<length;i+=2) sum += (p[i]<<8) | (i+1<length?p[i+1]:0);
  while(sum>>16) sum=(sum&0xffff)+(sum>>16);
  return htons((uint16_t)~sum);
}
static int fake_socket(int family,int type,int protocol) {
  assert(family==AF_INET && type==SOCK_RAW && protocol==IPPROTO_ICMP);
  ++sockets;
  return mode==1 ? -1 : 7;
}
static int fake_setsockopt(int fd,int level,int option,const void *value,socklen_t length) {
  const struct timeval *timeout=value;
  assert(fd==7 && level==SOL_SOCKET && option==SO_RCVTIMEO && length==sizeof(*timeout));
  assert(timeout->tv_sec==0 && timeout->tv_usec==50000);
  return mode==2 ? -1 : 0;
}
static ssize_t fake_sendto(int fd,const void *data,size_t length,int flags,
                         const struct sockaddr *address,socklen_t address_size) {
  assert(fd==7 && length==16 && flags==0 && address_size==sizeof(destination));
  ++sends; memcpy(sent,data,length); memcpy(&destination,address,address_size);
  assert(sent[0]==8 && inet_chksum(sent,sizeof(sent))==0);
  unsigned sequence=((unsigned)sent[6]<<8)|sent[7], index=sequence%8;
  if(host_sends[index]) assert(now-last_send[index]>=400000);
  ++host_sends[index]; last_send[index]=now;
  if(mode==3 || (mode==17 && sends==3)) return -1;
  if(mode==19) now+=5000;
  if(mode==4 || mode==19 || (mode==12 && sends==1) ||
     (mode==13 && (index&1)) || (mode==14 && host_sends[index]>1)) return length;
  assert(reply_count<64);
  reply_t *reply=&replies[reply_count++];
  memcpy(reply->echo,sent,sizeof(sent)); reply->source=destination;
  reply->ready=now+(mode==14 ? 450000 : mode==15 ? 100000+(7-index)*1000 : 0);
  if(mode==18 && index==0) {
    replies[reply_count++]=*reply;
  }
  return length;
}
static ssize_t fake_recvfrom(int fd,void *buffer,size_t size,int flags,
                           struct sockaddr *address,socklen_t *address_size) {
  assert(fd==7 && size>=36 && (flags==0 || flags==MSG_DONTWAIT) &&
         *address_size==sizeof(destination));
  ++receives;
  if(mode==5) { wifi_host_probe_cancel(); errno=EINTR; return -1; }
  if(mode==6) { errno=EIO; return -1; }
  reply_t *reply=NULL;
  for(unsigned i=0;i<reply_count;++i)
    if(!replies[i].used && (!reply || replies[i].ready<reply->ready)) reply=&replies[i];
  if(flags==0 && (!reply || reply->ready>now)) now+=50000;
  if(!reply || reply->ready>now) { errno=EAGAIN; return -1; }
  reply->used=true; ++delivered;
  if(mode==16 && delivered==2) wifi_host_probe_cancel();
  uint8_t *p=buffer; memset(p,0,36); p[0]=0x45; p[3]=36; p[9]=IPPROTO_ICMP;
  memcpy(p+20,reply->echo,sizeof(sent)); p[20]=0; p[22]=p[23]=0;
  if(mode==7) p[24]^=1;
  if(mode==8) p[26]^=1;
  if(mode==9) p[28]^=1;
  uint16_t sum=inet_chksum(p+20,16); memcpy(p+22,&sum,2);
  if(mode==10) p[22]^=1;
  struct sockaddr_in source=reply->source;
  if(mode==11) source.sin_addr.s_addr^=1;
  memcpy(address,&source,sizeof(source)); return 36;
}
static int fake_close(int fd) { assert(fd==7); ++closes; return 0; }
#define socket fake_socket
#define setsockopt fake_setsockopt
#define sendto fake_sendto
#define recvfrom fake_recvfrom
#define close fake_close
/* SOURCE */
#include "spi_protocol.h"
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static bool runner_busy, runner_fails, session_busy, tcp_cancelled;
static atomic_bool s_step_busy;
static void (*runner)(void);
static bool spi_bridge_async_scan_busy(void) { return runner_busy; }
static bool session_manager_is_active(void) { return session_busy; }
static bool spi_bridge_async_scan_start(void (*fn)(void)) {
  if(runner_busy || runner_fails) return false;
  runner=fn; runner_busy=true; return true;
}
static void port_scan_request_abort(void) { tcp_cancelled=true; }
static void reset(unsigned next_mode);
/* DISPATCH_DECLS */
static spi_status_t dispatch(spi_id_t id,const uint8_t *payload,uint8_t len,
                             uint8_t *out_resp_payload,uint8_t *out_resp_len) {
  /* DISPATCH_GUARD */
  switch(id) {
    /* DISPATCH_CASES */
    default: assert(false); return SPI_STATUS_ERROR;
  }
}
static void test_dispatcher(void) {
  _Static_assert(sizeof(spi_wifi_host_probe_status_t)==8,"wire status size");
  char ip[SPI_WIFI_HOST_PROBE_IP_LENGTH]="10.0.0.1", other[SPI_WIFI_HOST_PROBE_IP_LENGTH]="10.0.0.2";
  uint8_t length=0; spi_wifi_host_probe_status_t status;
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_START,(void *)ip,0,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  char invalid[16]; memset(invalid,'x',sizeof(invalid));
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_START,(void *)invalid,16,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  session_busy=true;
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_START,(void *)ip,16,NULL,NULL)==SPI_STATUS_BUSY);
  session_busy=false; atomic_store(&s_step_busy,true);
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_START,(void *)ip,16,NULL,NULL)==SPI_STATUS_BUSY);
  atomic_store(&s_step_busy,false);
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_START,(void *)ip,16,NULL,NULL)==SPI_STATUS_OK);
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_START,(void *)other,16,NULL,NULL)==SPI_STATUS_BUSY);
  assert(!strcmp(s_probe_ip,ip));
  dispatch(SPI_ID_WIFI_HOST_PROBE_STATUS,NULL,0,(void *)&status,&length);
  assert(length==8 && status.is_running && !status.is_reachable);
  mode=0; runner(); runner_busy=false;
  dispatch(SPI_ID_WIFI_HOST_PROBE_STATUS,NULL,0,(void *)&status,&length);
  assert(!status.is_running && status.is_reachable && status.error==ESP_OK);
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_START,(void *)ip,16,NULL,NULL)==SPI_STATUS_OK);
  assert(dispatch(SPI_ID_WIFI_PORT_SCAN_STOP,NULL,0,NULL,NULL)==SPI_STATUS_OK);
  assert(tcp_cancelled); runner(); runner_busy=false;
  dispatch(SPI_ID_WIFI_HOST_PROBE_STATUS,NULL,0,(void *)&status,&length);
  assert(status.error==ESP_ERR_INVALID_STATE && !status.is_running && !status.is_reachable);
  runner_fails=true;
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_START,(void *)ip,16,NULL,NULL)==SPI_STATUS_BUSY);
  dispatch(SPI_ID_WIFI_HOST_PROBE_STATUS,NULL,0,(void *)&status,&length);
  assert(status.error==ESP_ERR_INVALID_STATE && !status.is_running);
  runner_fails=false; reset(0);
  _Static_assert(sizeof(spi_wifi_host_probe_batch_req_t)==8,"batch request size");
  _Static_assert(sizeof(spi_wifi_host_probe_batch_status_t)==12,"batch status size");
  spi_wifi_host_probe_batch_req_t batch={.first_ip=htonl(0x0a000001),.count=8};
  spi_wifi_host_probe_batch_status_t batch_status;
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,7,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  batch.count=0;
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,8,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  batch.count=9;
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,8,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  batch.count=8; batch.reserved[2]=1;
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,8,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  batch.reserved[2]=0; batch.first_ip=htonl(UINT32_MAX-3);
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,8,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  batch.first_ip=htonl(0x0a000001);
  session_busy=true;
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,8,NULL,NULL)==SPI_STATUS_BUSY);
  session_busy=false; atomic_store(&s_step_busy,true);
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,8,NULL,NULL)==SPI_STATUS_BUSY);
  atomic_store(&s_step_busy,false);
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,8,NULL,NULL)==SPI_STATUS_OK);
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_START,(void *)ip,16,NULL,NULL)==SPI_STATUS_BUSY);
  batch.count=3;
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,8,NULL,NULL)==SPI_STATUS_BUSY);
  dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_STATUS,NULL,0,(void *)&batch_status,&length);
  assert(length==12 && batch_status.count==8 && batch_status.is_running);
  mode=0; runner(); runner_busy=false;
  dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_STATUS,NULL,0,(void *)&batch_status,&length);
  assert(batch_status.error==ESP_OK && batch_status.reachable_mask==255 && !batch_status.is_running);
  assert(batch_status.first_ip==batch.first_ip);
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,8,NULL,NULL)==SPI_STATUS_OK);
  dispatch(SPI_ID_WIFI_PORT_SCAN_STOP,NULL,0,NULL,NULL); runner(); runner_busy=false;
  dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_STATUS,NULL,0,(void *)&batch_status,&length);
  assert(batch_status.error==ESP_ERR_INVALID_STATE && !batch_status.is_running && !batch_status.reachable_mask);
  runner_fails=true;
  assert(dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_START,(void *)&batch,8,NULL,NULL)==SPI_STATUS_BUSY);
  dispatch(SPI_ID_WIFI_HOST_PROBE_BATCH_STATUS,NULL,0,(void *)&batch_status,&length);
  assert(batch_status.error==ESP_ERR_INVALID_STATE && !batch_status.is_running);
}
static void reset(unsigned next_mode) {
  now=sends=receives=closes=sockets=delivered=reply_count=0; mode=next_mode;
  memset(replies,0,sizeof(replies)); memset(host_sends,0,sizeof(host_sends));
  memset(last_send,0,sizeof(last_send)); wifi_host_probe_reset();
}
static void test_batches(void) {
  uint16_t reachable;
  uint32_t first=htonl(0x0a000001);
  reset(0);
  assert(wifi_host_probe_run_batch(first,0,&reachable)==ESP_ERR_INVALID_ARG);
  assert(wifi_host_probe_run_batch(first,9,&reachable)==ESP_ERR_INVALID_ARG);
  assert(wifi_host_probe_run_batch(htonl(UINT32_MAX-3),8,&reachable)==ESP_ERR_INVALID_ARG);
  assert(wifi_host_probe_run_batch(first,8,NULL)==ESP_ERR_INVALID_ARG && sockets==0);
  assert(wifi_host_probe_run_batch(first,8,&reachable)==ESP_OK && reachable==255);
  assert(sends==8 && sockets==1 && closes==1 && now==0);
  reset(4);
  assert(wifi_host_probe_run_batch(first,8,&reachable)==ESP_OK && reachable==0);
  assert(sends==16 && sockets==1 && closes==1 && now==800000);
  reset(13);
  assert(wifi_host_probe_run_batch(first,8,&reachable)==ESP_OK && reachable==0x55);
  assert(sends==12 && now==800000);
  for(unsigned i=0;i<8;++i) assert(host_sends[i]==((i&1)?2:1));
  reset(14);
  assert(wifi_host_probe_run_batch(first,8,&reachable)==ESP_OK && reachable==255);
  assert(sends==16 && now==450000); // First-attempt echoes arriving after retry still count.
  reset(15);
  assert(wifi_host_probe_run_batch(first,8,&reachable)==ESP_OK && reachable==255);
  assert(sends==8 && now==150000); // Responses arrive in reverse host order.
  reset(16);
  assert(wifi_host_probe_run_batch(first,8,&reachable)==ESP_ERR_INVALID_STATE && reachable==3);
  assert(sends==2 && closes==1);
  reset(17);
  assert(wifi_host_probe_run_batch(first,8,&reachable)==ESP_FAIL && reachable==3);
  assert(sends==3 && closes==1);
  reset(18);
  assert(wifi_host_probe_run_batch(first,8,&reachable)==ESP_OK && reachable==255);
  assert(sends==8 && delivered==9);
  reset(19);
  assert(wifi_host_probe_run_batch(first,8,&reachable)==ESP_OK && reachable==0);
  assert(sends==16 && now>=840000 && now<900000); // Every host retains its own window.
  reset(0);
  assert(wifi_host_probe_run_batch(htonl(UINT32_MAX-1),2,&reachable)==ESP_OK && reachable==3);
}
int main(void) {
  bool reachable;
  reset(0); assert(wifi_host_probe_run(NULL,&reachable)==ESP_ERR_INVALID_ARG);
  assert(wifi_host_probe_run("10.0.0.1",NULL)==ESP_ERR_INVALID_ARG);
  assert(wifi_host_probe_run("not-ip",&reachable)==ESP_ERR_INVALID_ARG);
  wifi_host_probe_cancel(); assert(wifi_host_probe_run("10.0.0.1",&reachable)==ESP_ERR_INVALID_STATE);
  for(unsigned m=0;m<=12;++m) {
    reset(m); int error=wifi_host_probe_run("10.0.0.1",&reachable);
    if(m==1 || m==2 || m==3 || m==6) assert(error==ESP_FAIL && !reachable);
    else if(m==5) assert(error==ESP_ERR_INVALID_STATE && !reachable && receives==1);
    else assert(error==ESP_OK && reachable==(m==0 || m==12));
    assert(closes==(m==1 ? 0 : 1));
    if(m>=7 && m<=11) assert(sends==2 && now==800000);
    if(m==12) assert(sends==2 && now==400000);
  }
  uint8_t frame[128]={0x45}; frame[3]=36; frame[9]=IPPROTO_ICMP;
  memcpy(frame+20,sent,16); frame[20]=0; frame[22]=frame[23]=0;
  uint16_t sum=inet_chksum(frame+20,16); memcpy(frame+22,&sum,2);
  uint16_t sequence;
  assert(matches_reply(frame,36,sent,&sequence));
  for(unsigned length=0;length<36;++length) assert(!matches_reply(frame,length,sent,&sequence));
  frame[3]=128; assert(!matches_reply(frame,36,sent,&sequence)); frame[3]=36;
  frame[0]=0x4f; assert(!matches_reply(frame,36,sent,&sequence));
  // Every byte/length combination remains bounded, including malicious IHL/length values.
  for(unsigned i=0;i<sizeof(frame);++i) for(unsigned value=0;value<256;++value) {
    uint8_t old=frame[i]; frame[i]=value;
    for(unsigned length=0;length<=sizeof(frame);++length) (void)matches_reply(frame,length,sent,&sequence);
    frame[i]=old;
  }
  test_batches(); reset(0); test_dispatcher();
  puts("PASS: eight hosts per socket, 800ms silent batch, selective retries, late/out-of-order/duplicate replies, partial cancellation, dispatcher ownership/status/validation, failures and bounded parser");
}
"""
with tempfile.TemporaryDirectory(prefix="highboy-host-probe-") as tmp:
    work = Path(tmp)
    for name in ("esp_log.h", "esp_random.h", "esp_timer.h", "lwip/inet_chksum.h", "lwip/sockets.h"):
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(NOTICE + "#pragma once\n" + (
            "#define ESP_LOGE(tag, ...) ((void)(tag))\n" if name == "esp_log.h" else ""))
    (work / "esp_err.h").write_text(NOTICE + "#pragma once\ntypedef int esp_err_t;\n"
        "#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n")
    dispatcher = (WIFI.parent / "spi_bridge/wifi_dispatcher.c").read_text()
    declarations = dispatcher[dispatcher.index("static portMUX_TYPE s_probe_lock"):dispatcher.index("static port_scan_dispatch_req_t s_port_scan_req")]
    for worker in ("host_probe_worker", "host_probe_batch_worker"):
        begin = dispatcher.index("static void " + worker + "(void) {")
        declarations += dispatcher[begin:dispatcher.index("\n\n", begin)] + "\n"
    begin = dispatcher.index("  if (((id >= SPI_ID_WIFI_PORT_SCAN_TARGET_RANGE")
    guard = dispatcher[begin:dispatcher.index("\n\n", begin)]
    begin = dispatcher.index("    case SPI_ID_WIFI_HOST_PROBE_START:")
    cases = dispatcher[begin:dispatcher.index("    case SPI_ID_WIFI_PORT_SCAN_TARGET_RANGE:", begin)]
    begin = dispatcher.index("    case SPI_ID_WIFI_PORT_SCAN_STOP:")
    cases += dispatcher[begin:dispatcher.index("    case SPI_ID_WIFI_GET_MAC:", begin)]
    code = CODE.replace("/* DISPATCH_DECLS */", declarations).replace("/* DISPATCH_GUARD */", guard).replace("/* DISPATCH_CASES */", cases)
    (work / "esp_rom_crc.h").write_text(NOTICE + "#pragma once\nstatic uint16_t esp_rom_crc16_le(uint16_t crc, const uint8_t *data, uint32_t len) { (void)data; (void)len; return crc; }\n")
    (work / "test.c").write_text(NOTICE + code.replace("/* SOURCE */", '#include "' + str(WIFI / "wifi_host_probe.c") + '"'))
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I" + str(work),
        "-I" + str(WIFI / "include"), "-I" + str(WIFI.parent / "spi_bridge/include"), str(work / "test.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True, timeout=30)
