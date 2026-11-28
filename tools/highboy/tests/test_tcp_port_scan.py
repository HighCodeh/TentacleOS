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

"""Run the C5 TCP scanner with timed fake sockets, never a real network."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
WIFI = ROOT / "firmware_c5/components/Applications/wifi"
P4_WIFI = ROOT / "firmware_p4/components/Applications/wifi"
NOTICE = (WIFI / "port_scan.c").read_text().split("#include", 1)[0]
CODE = r"""
#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "port_scan.h"

#define TEST_FD_BASE 10
#define TEST_SOCKET_COUNT 8
#define TEST_NEVER INT64_MAX
typedef struct {
  bool used, is_connected, reported, sent;
  int port, flags, error;
  int64_t ready;
} test_socket_t;
typedef struct { int port, error; int64_t delay; bool immediate; } test_reply_t;
static test_socket_t sockets[TEST_SOCKET_COUNT];
static test_reply_t replies[128];
static unsigned reply_count, opens, closes, selections, banners, interrupts, cancel_select;
static unsigned socket_limit, flag_fail, option_fail;
static bool select_fails, use_asset, exception_only, passive_silent, no_banner, banner_setup_fails;
static int64_t now;
static void test_log(const char *format, ...) { (void)format; }
static test_socket_t *lookup(int fd) {
  assert(fd>=TEST_FD_BASE && fd<TEST_FD_BASE+TEST_SOCKET_COUNT);
  assert(sockets[fd-TEST_FD_BASE].used);
  return &sockets[fd-TEST_FD_BASE];
}
static int64_t esp_timer_get_time(void) { return now; }
static void vTaskDelay(unsigned ticks) { now+=ticks*1000LL; }
static void configure(int port, int error, int64_t delay, bool immediate) {
  assert(reply_count<128);
  replies[reply_count++]=(test_reply_t){port,error,delay,immediate};
}
static int fake_socket(int family, int type, int protocol) {
  assert(family==AF_INET && type==SOCK_STREAM && protocol==IPPROTO_IP);
  for(unsigned i=0;i<socket_limit;++i) if(!sockets[i].used) {
    sockets[i]=(test_socket_t){.used=true}; ++opens; return TEST_FD_BASE+i;
  }
  errno=EMFILE; return -1;
}
static int fake_connect(int fd, const struct sockaddr *address, socklen_t size) {
  assert(size==sizeof(struct sockaddr_in));
  test_socket_t *socket=lookup(fd);
  assert(socket->flags&O_NONBLOCK);
  socket->port=ntohs(((const struct sockaddr_in *)address)->sin_port);
  test_reply_t reply={.port=socket->port,.error=ECONNREFUSED,.delay=1000};
  for(unsigned i=0;i<reply_count;++i) if(replies[i].port==socket->port) reply=replies[i];
  socket->error=reply.error;
  socket->ready=reply.delay==TEST_NEVER ? TEST_NEVER : now+reply.delay;
  if(reply.immediate) {
    socket->is_connected=reply.error==0;
    errno=reply.error; return reply.error ? -1 : 0;
  }
  errno=EINPROGRESS; return -1;
}
static int fake_fcntl(int fd, int operation, int flags) {
  test_socket_t *socket=lookup(fd);
  if(flag_fail && --flag_fail==0) { errno=EIO; return -1; }
  if(operation==F_GETFL) return socket->flags;
  assert(operation==F_SETFL); socket->flags=flags; return 0;
}
static int fake_select(int nfds, fd_set *readers, fd_set *writers, fd_set *errors,
                       struct timeval *timeout) {
  assert(readers==NULL && writers!=NULL && nfds<=TEST_FD_BASE+TEST_SOCKET_COUNT);
  ++selections;
  if(select_fails) { errno=EIO; return -1; }
  if(interrupts) { --interrupts; now+=100; errno=EINTR; return -1; }
  int64_t deadline=now+timeout->tv_sec*1000000LL+timeout->tv_usec;
  int64_t wake=deadline;
  for(int fd=TEST_FD_BASE;fd<nfds;++fd) if(FD_ISSET(fd,writers)) {
    test_socket_t *socket=lookup(fd);
    assert(!socket->reported);
    if(socket->ready<wake) wake=socket->ready;
  }
  if(wake>now) now=wake;
  int count=0;
  for(int fd=TEST_FD_BASE;fd<nfds;++fd) {
    bool ready=FD_ISSET(fd,writers) && lookup(fd)->ready<=now;
    if(!ready || exception_only) FD_CLR(fd,writers);
    if(errors && (!ready || !exception_only)) FD_CLR(fd,errors);
    if(ready) ++count;
  }
  if(cancel_select==selections) port_scan_request_abort();
  return count;
}
static int fake_getsockopt(int fd, int level, int option, void *value, socklen_t *size) {
  test_socket_t *socket=lookup(fd);
  assert(level==SOL_SOCKET && option==SO_ERROR && *size==sizeof(int));
  assert(socket->ready<=now);
  if(option_fail && --option_fail==0) { errno=EIO; return -1; }
  *(int *)value=socket->error; socket->reported=true;
  socket->is_connected=socket->error==0; return 0;
}
static int fake_setsockopt(int fd, int level, int option, const void *value, socklen_t size) {
  assert(lookup(fd)->is_connected && level==SOL_SOCKET);
  assert((option==SO_RCVTIMEO || option==SO_SNDTIMEO) && size==sizeof(struct timeval));
  const struct timeval *timeout=value;
  assert(timeout->tv_sec==0 && timeout->tv_usec==250000);
  if(banner_setup_fails) { errno=EIO; return -1; }
  return 0;
}
static ssize_t fake_recv(int fd, void *buffer, size_t size, int flags) {
  test_socket_t *socket=lookup(fd); assert(socket->is_connected && flags==0); ++banners;
  if(no_banner || (passive_silent && !socket->sent)) { errno=EAGAIN; return -1; }
  const char *banner=socket->port==22 ? "SSH-2.0-OpenSSH_10.5\r\n" :
                                      "HTTP/1.0 200 OK\r\nServer: AirTunes/1005.13.5\r\n\r\n";
  assert(size>strlen(banner)); memcpy(buffer,banner,strlen(banner)); return strlen(banner);
}
static ssize_t fake_send(int fd, const void *data, size_t size, int flags) {
  test_socket_t *socket=lookup(fd); assert(socket->is_connected); socket->sent=true; return size;
}
static int fake_close(int fd) { lookup(fd)->used=false; ++closes; return 0; }
#define socket fake_socket
#define connect fake_connect
#define fcntl fake_fcntl
#define select fake_select
#define getsockopt fake_getsockopt
#define setsockopt fake_setsockopt
#define recv fake_recv
#define send fake_send
#define close fake_close
/* SCANNER */
static FILE *port_file_open(const char *path,const char *mode) {
  return use_asset && !strcmp(path,"/assets/config/wifi/common_ports.txt")
      ? fopen(TEST_PORT_FILE,mode) : NULL;
}
#define TAG COMMON_TAG
#define fopen port_file_open
/* COMMON */
#undef fopen
#undef TAG
static void reset(void) {
  assert(opens==closes);
  memset(sockets,0,sizeof(sockets));
  reply_count=opens=closes=selections=banners=interrupts=cancel_select=0;
  flag_fail=option_fail=0; select_fails=exception_only=false; socket_limit=TEST_SOCKET_COUNT;
  passive_silent=no_banner=banner_setup_fails=false;
  now=0; port_scan_reset_abort();
}
static bool has_port(const port_scan_result_t *results,int count,int port) {
  for(int i=0;i<count;++i) if(results[i].port==port) return true;
  return false;
}
static int scan(const int *ports,int count,port_scan_result_t *results,int capacity) {
  int found=port_scan_target_list("192.168.0.107",ports,count,results,capacity);
  assert(opens==closes); return found;
}
int main(void) {
  port_scan_result_t results[32]; int ports[COMMON_PORTS_MAX];
  // One fast RST must not discard the SSH connection still completing in that batch.
  for(unsigned asset=0;asset<2;++asset) {
    reset(); use_asset=asset; int count=common_ports_load(ports,COMMON_PORTS_MAX);
    assert(count>8); bool contains_ssh=false;
    for(int i=0;i<count;++i) contains_ssh|=ports[i]==22;
    assert(contains_ssh);
    configure(22,0,180000,false); configure(5000,0,500,false);
    int found=scan(ports,count,results,32);
    assert(found==2 && has_port(results,found,22) && has_port(results,found,5000));
    assert(!strcmp(results[0].banner,"SSH-2.0-OpenSSH_10.5"));
  }
  reset(); int custom[]={22,80,443,8000,8001,8002,8003,8004};
  configure(22,0,180000,false);
  for(unsigned i=1;i<8;++i) configure(custom[i],0,TEST_NEVER,false);
  assert(scan(custom,8,results,32)==1 && results[0].port==22);
  assert(now>=1000000 && now<=1010000);
  reset(); int pair[]={22,80}; configure(22,0,999000,false);
  assert(scan(pair,2,results,32)==1 && results[0].port==22);
  reset(); configure(22,0,1001000,false); assert(scan(pair,2,results,32)==0);
  assert(now>=1000000 && now<=1010000);
  reset(); configure(22,0,TEST_NEVER,false); configure(80,0,TEST_NEVER,false); interrupts=3;
  assert(scan(pair,2,results,32)==0 && now<=1010000);
  reset(); configure(22,0,0,true); assert(scan(pair,1,results,32)==1);
  reset(); configure(22,ECONNREFUSED,0,true); assert(scan(pair,1,results,32)==0 && selections==0);
  reset(); configure(22,0,10000,false); no_banner=true;
  assert(scan(pair,1,results,32)==1 && !strcmp(results[0].banner,"(Without Banner)"));
  reset(); configure(22,0,10000,false); banner_setup_fails=true;
  assert(scan(pair,1,results,32)==1 && banners==0);
  reset(); configure(80,0,10000,false); passive_silent=true;
  assert(scan(pair+1,1,results,32)==1 && !strcmp(results[0].banner,"AirTunes/1005.13.5"));
  reset(); select_fails=true; assert(scan(pair,2,results,32)==0 && banners==0);
  reset(); exception_only=true; assert(scan(pair,2,results,32)==0);
  reset(); option_fail=1; configure(22,0,1000,false); assert(scan(pair,1,results,32)==0);
  reset(); cancel_select=2; configure(22,0,10000,false); configure(80,0,TEST_NEVER,false);
  assert(scan(pair,2,results,32)==1 && results[0].port==22);
  assert(now<=100000 && banners==0);
  reset(); port_scan_request_abort(); assert(scan(pair,2,results,32)==0 && opens==0);
  reset(); for(unsigned i=0;i<8;++i) { ports[i]=100+i; configure(ports[i],0,i*10000,false); }
  assert(scan(ports,8,results,1)==1 && opens==8 && closes==8);
  reset(); flag_fail=1; configure(22,0,0,true); assert(scan(pair,1,results,32)==0);
  reset(); socket_limit=0; assert(scan(pair,2,results,32)==0 && selections==0);
  puts("PASS: Common and Custom retain late SSH, per-batch deadlines, all ready sockets, errors, cancellation and cleanup");
}
"""
with tempfile.TemporaryDirectory(prefix="highboy-tcp-scan-") as temporary:
    work = Path(temporary)
    for name in ("esp_log.h", "esp_timer.h", "freertos/FreeRTOS.h", "freertos/task.h",
                 "lwip/err.h", "lwip/netdb.h", "lwip/sockets.h", "lwip/sys.h"):
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(NOTICE + "#pragma once\n")
    (work / "esp_log.h").write_text(NOTICE + "\n".join(
        f"#define ESP_LOG{level}(tag, ...) ((void)(tag), test_log(__VA_ARGS__))" for level in "IWEVD") + "\n")
    (work / "freertos/FreeRTOS.h").write_text(NOTICE + "#define portTICK_PERIOD_MS 1\n")
    (work / "esp_attr.h").write_text(NOTICE + "#define EXT_RAM_BSS_ATTR\n")
    code = CODE.replace("/* SCANNER */", f'#include "{WIFI / "port_scan.c"}"')
    code = code.replace("/* COMMON */", f'#include "{P4_WIFI / "common_ports.c"}"')
    (work / "test.c").write_text(NOTICE + code)
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-unused-function", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-I" + str(work), "-I" + str(WIFI / "include"),
                    "-I" + str(P4_WIFI / "include"),
                    "-DTEST_PORT_FILE=" + json.dumps(str(ROOT / "firmware_p4/assets/config/wifi/common_ports.txt")),
                    str(work / "test.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True, timeout=20)
