// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "wifi_udp_scan.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGE(tag, ...) ((void)(tag))
static const char *esp_err_to_name(int error) { return "mock"; }
static int64_t now;
static unsigned fault, mode, sockets, open_fds, peak_fds, closes, sends, selects;
static unsigned received_count, cancel_after, cancel_send, allocations, frees;
static int raw_fd;
static unsigned log_count;
static char log_lines[32][160];
static void log_info(const char *tag,const char *format,...) {
  assert(open_fds==0 && log_count<32);
  va_list args; va_start(args,format);
  int length=vsnprintf(log_lines[log_count],sizeof(log_lines[0]),format,args);
  va_end(args); assert(length>0 && length<(int)sizeof(log_lines[0]));
  ++log_count;
  now+=1000000; // A slow serial sink must not consume any active probe's deadline.
}
#define ESP_LOGI(tag, ...) log_info(tag, __VA_ARGS__)
typedef struct { bool used, raw, replied; uint16_t port, local; unsigned attempts; size_t bytes; } socket_t;
static socket_t fd_map[128];
static uint8_t icmp_queue[64][56];
static unsigned icmp_write, icmp_read;
static wifi_udp_scan_result_t results[32];
static int64_t esp_timer_get_time(void) { return now; }
static uint32_t esp_random(void) { return 0x12345678; }
static uint16_t inet_chksum(const void *data, uint16_t length) {
  const uint8_t *p=data; uint32_t sum=0;
  for(unsigned i=0;i<length;i+=2) sum+=(p[i]<<8)|(i+1<length?p[i+1]:0);
  while(sum>>16) sum=(sum&0xffff)+(sum>>16);
  return htons((uint16_t)~sum);
}
static void *heap_caps_calloc(size_t count,size_t bytes,unsigned caps) {
  assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(fault==1) return NULL;
  ++allocations; return calloc(count,bytes);
}
static void fake_free(void *p) { if(p) ++frees; free(p); }
static void put16(uint8_t *p,uint16_t n) { p[0]=n>>8; p[1]=n; }
static void make_icmp(uint8_t *p,uint16_t port,uint16_t local,size_t bytes,uint8_t code) {
  memset(p,0,56); p[0]=0x45; put16(p+2,56); p[9]=IPPROTO_ICMP;
  uint32_t source=inet_addr("10.0.0.1"), dest=inet_addr("10.0.0.2");
  memcpy(p+12,&source,4); memcpy(p+16,&dest,4);
  p[20]=3; p[21]=code; p[28]=0x45; put16(p+30,28+bytes); p[37]=IPPROTO_UDP;
  memcpy(p+40,&dest,4); memcpy(p+44,&source,4);
  put16(p+48,local); put16(p+50,port); put16(p+52,8+bytes);
  uint16_t checksum=inet_chksum(p+20,36); memcpy(p+22,&checksum,2);
}
static int fake_socket(int family,int type,int protocol) {
  assert(family==AF_INET);
  assert((type==SOCK_RAW && protocol==IPPROTO_ICMP) || (type==SOCK_DGRAM && protocol==IPPROTO_UDP));
  if(fault==2 || (fault==3 && type==SOCK_DGRAM)) { errno=EMFILE; return -1; }
  int fd=7+sockets++; assert(fd<128); fd_map[fd]=(socket_t){.used=true,.raw=type==SOCK_RAW,.local=40000+fd};
  if(type==SOCK_RAW) raw_fd=fd;
  if(++open_fds>peak_fds) peak_fds=open_fds;
  return fd;
}
static int fake_fcntl(int fd,int command,...) {
  assert(fd_map[fd].used && (command==F_GETFL || command==F_SETFL));
  if(fault==4 || (fault==5 && !fd_map[fd].raw)) { errno=EIO; return -1; }
  return 0;
}
static int fake_connect(int fd,const struct sockaddr *address,socklen_t length) {
  const struct sockaddr_in *dest=(const void *)address;
  assert(fd_map[fd].used && length==sizeof(*dest) && dest->sin_family==AF_INET);
  assert(dest->sin_addr.s_addr==inet_addr("10.0.0.1"));
  fd_map[fd].port=ntohs(dest->sin_port);
  if(fault==6) { errno=ENETUNREACH; return -1; }
  return 0;
}
static int fake_getsockname(int fd,struct sockaddr *address,socklen_t *length) {
  if(fault==7) { errno=EIO; return -1; }
  assert(*length==sizeof(struct sockaddr_in));
  struct sockaddr_in local={.sin_family=AF_INET,.sin_port=htons(fd_map[fd].local)};
  local.sin_addr.s_addr=inet_addr("10.0.0.2"); memcpy(address,&local,sizeof(local)); return 0;
}
static ssize_t fake_send(int fd,const void *data,size_t length,int flags) {
  socket_t *s=&fd_map[fd]; assert(s->used && !s->raw && flags==0);
  ++sends; ++s->attempts; s->bytes=length;
  if(sends==cancel_send) wifi_udp_scan_cancel();
  if(fault==8) { errno=ENOMEM; return -1; }
  if(s->port==10005) { errno=ECONNREFUSED; return -1; }
  const uint8_t *p=data;
  if(s->port==53) {
    assert(length==17 && p[2]==0 && p[3]==0 && p[4]==0 && p[5]==1);
    assert(p[12]==0 && p[13]==0 && p[14]==2 && p[15]==0 && p[16]==1);
  } else if(s->port==123) {
    assert(length==48 && p[0]==0x23); uint32_t nonce; memcpy(&nonce,p+40,4); assert(nonce==esp_random());
  } else assert(length==0);
  if(mode==0 && (s->port==10003 || s->port==10004 || s->port==10007)) {
    assert(icmp_write<64); uint8_t *p=icmp_queue[icmp_write++];
    make_icmp(p,s->port,s->local,length,s->port==10004 ? 13 : 3);
    if(s->port==10007) p[22]^=1;
  }
  return length;
}
static bool has_reply(socket_t *s) {
  if(s->raw || !s->used || s->replied) return false;
  if(mode==1) return false;
  if(mode==2) return true;
  return s->port==53 || s->port==123 || s->port==10000 || s->port==10001 ||
         (s->port==10006 && s->attempts>=2);
}
static int fake_select(int n,fd_set *read,fd_set *write,fd_set *except,struct timeval *timeout) {
  assert(write==NULL && except==NULL && timeout->tv_sec==0 && timeout->tv_usec<=50000);
  ++selects; now+=timeout->tv_usec;
  if(fault==9) { errno=EIO; return -1; }
  if(fault==10) { errno=EINTR; return -1; }
  int count=0;
  for(int fd=0;fd<n;++fd) if(FD_ISSET(fd,read)) {
    assert(fd_map[fd].used);
    if((fd==raw_fd && icmp_read<icmp_write) || has_reply(&fd_map[fd])) ++count;
    else FD_CLR(fd,read);
  }
  return count;
}
static ssize_t fake_recv(int fd,void *data,size_t length,int flags) {
  assert(fd_map[fd].used && length==512 && flags==0);
  if(fault==11 || (fault==12 && fd==raw_fd)) { errno=EIO; return -1; }
  if(fd==raw_fd) { assert(icmp_read<icmp_write); memcpy(data,icmp_queue[icmp_read++],56); return 56; }
  socket_t *s=&fd_map[fd]; assert(has_reply(s)); s->replied=true;
  if(s->port==10001) return 0;
  uint8_t reply[]={ 'O','K',0,0xff,'\n' }; memcpy(data,reply,sizeof(reply)); return sizeof(reply);
}
static int fake_close(int fd) {
  assert(fd>=0 && fd_map[fd].used && open_fds); fd_map[fd].used=false; --open_fds; ++closes; return 0;
}
static void hit(const wifi_udp_scan_result_t *result,void *context) {
  assert(context==(void *)0x1234 && received_count<32);
  for(unsigned i=0;i<received_count;++i) assert(results[i].port!=result->port);
  assert(memchr(result->banner,0,sizeof(result->banner))!=NULL);
  results[received_count++]=*result;
  if(received_count==cancel_after) wifi_udp_scan_cancel();
}
#define socket fake_socket
#define fcntl fake_fcntl
#define connect fake_connect
#define getsockname fake_getsockname
#define send fake_send
#define recv fake_recv
#define select fake_select
#define close fake_close
#define free fake_free
/* SOURCE */
#undef free
static void reset(void) {
  assert(open_fds==0 && allocations==frees);
  now=fault=mode=sockets=peak_fds=closes=sends=selects=received_count=log_count=0;
  cancel_after=cancel_send=allocations=frees=icmp_write=icmp_read=0;
  memset(fd_map,0,sizeof(fd_map)); memset(results,0,sizeof(results)); wifi_udp_scan_reset();
}
static esp_err_t run(const int *ports,uint16_t count) {
  const wifi_udp_scan_request_t request={.ip="10.0.0.1",.ports=ports,.count=count,.on_result=hit,.context=(void *)0x1234};
  esp_err_t err=wifi_udp_scan_run(&request);
  assert(open_fds==0 && allocations==frees && closes==sockets && peak_fds<=5);
  return err;
}
static void test_parser(void) {
  wifi_udp_scan_batch_t batch={.target_ip=inet_addr("10.0.0.1"),.count=1};
  batch.ports[0]=(wifi_udp_scan_port_t){.port=53,.local_port=40000,.local_ip=inet_addr("10.0.0.2"),.attempts=1,.payload_size=17};
  wifi_udp_scan_request_t request={.on_result=hit,.context=(void *)0x1234};
  uint8_t valid[56]; make_icmp(valid,53,40000,17,3);
  memcpy(batch.reply,valid,56); classify_icmp(&batch,56,&request); assert(batch.ports[0].is_done && received_count==0);
  for(unsigned len=0;len<56;++len) {
    batch.ports[0].is_done=false; classify_icmp(&batch,len,&request); assert(!batch.ports[0].is_done);
  }
  const unsigned offsets[]={0,2,6,9,16,20,22,28,30,34,37,40,44,48,50,52};
  for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);++i) {
    memcpy(batch.reply,valid,56); batch.reply[offsets[i]]^=1;
    if(offsets[i]>=28) { batch.reply[22]=batch.reply[23]=0; uint16_t sum=inet_chksum(batch.reply+20,36); memcpy(batch.reply+22,&sum,2); }
    batch.ports[0].is_done=false; classify_icmp(&batch,56,&request); assert(!batch.ports[0].is_done);
  }
  const uint8_t codes[]={0,1,2,9,10,13};
  for(unsigned i=0;i<sizeof(codes);++i) {
    received_count=0; make_icmp(batch.reply,53,40000,17,codes[i]); batch.ports[0].is_done=false;
    classify_icmp(&batch,56,&request); assert(batch.ports[0].is_done && received_count==1 && results[0].status==WIFI_UDP_SCAN_FILTERED);
  }
  received_count=0; make_icmp(batch.reply,53,40000,17,4); batch.ports[0].is_done=false;
  classify_icmp(&batch,56,&request); assert(!batch.ports[0].is_done && received_count==0);
  // Check malformed packet lengths and headers against ASan/UBSan.
  for(unsigned i=0;i<56;++i) for(unsigned value=0;value<256;++value) {
    memcpy(batch.reply,valid,56); batch.reply[i]=value;
    for(unsigned len=0;len<=64;++len) { batch.ports[0].is_done=false; classify_icmp(&batch,len,&request); }
  }
}
#include "spi_protocol.h"
#include "port_scan.h"
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static bool runner_busy, runner_fails, session_busy, tcp_cancelled, probe_cancelled;
static atomic_bool s_step_busy;
static void (*runner)(void);
static bool spi_bridge_async_scan_busy(void) { return runner_busy; }
static bool session_manager_is_active(void) { return session_busy; }
static bool spi_bridge_async_scan_start(void (*fn)(void)) {
  if(runner_busy || runner_fails) return false;
  runner=fn; runner_busy=true; return true;
}
void port_scan_request_abort(void) { tcp_cancelled=true; }
static void wifi_host_probe_cancel(void) { probe_cancelled=true; }
static const void *published_records;
static const uint16_t *published_count;
static void spi_bridge_provide_results_dynamic(const void *records,const uint16_t *count,size_t bytes) {
  assert(bytes==sizeof(spi_port_scan_result_t)); published_records=records; published_count=count;
}
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
  _Static_assert(sizeof(spi_wifi_udp_scan_status_t)==8,"UDP wire status size");
  uint8_t payload[28]={0}, length; strcpy((char *)payload,"10.0.0.1");
  uint16_t cap=32,count=4,ports[]={53,123,10000,10001};
  memcpy(payload+16,&cap,2); memcpy(payload+18,&count,2); memcpy(payload+20,ports,8);
  spi_wifi_udp_scan_status_t status;
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,0,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,27,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  payload[18]=33;
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  payload[18]=4; payload[16]=3;
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  payload[16]=32; memset(payload+20,0,2);
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  memcpy(payload+20,ports,8); payload[0]='x';
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_INVALID_ARG);
  payload[0]='1';
  session_busy=true;
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_BUSY);
  session_busy=false; atomic_store(&s_step_busy,true);
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_BUSY);
  atomic_store(&s_step_busy,false);
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_OK);
  assert(published_records==s_port_scan_records && published_count==&s_port_scan_count);
  payload[0]='9';
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_BUSY);
  assert(!strcmp(s_port_scan_req.ip,"10.0.0.1") && s_port_scan_req.ports[0]==53);
  payload[0]='1'; dispatch(SPI_ID_WIFI_UDP_SCAN_STATUS,NULL,0,(void *)&status,&length);
  assert(length==8 && status.is_running && status.error==ESP_OK);
  runner(); runner_busy=false; dispatch(SPI_ID_WIFI_UDP_SCAN_STATUS,NULL,0,(void *)&status,&length);
  assert(!status.is_running && status.error==ESP_OK && s_port_scan_count==4 && log_count==4);
  assert(!strcmp(log_lines[0],"HIT: 10.0.0.1:53 [UDP] OPEN | OK..."));
  assert(!strcmp(log_lines[3],"HIT: 10.0.0.1:10001 [UDP] OPEN | (Without Banner)"));
  for(unsigned i=0;i<4;++i) {
    assert(s_port_scan_records[i].protocol==PORT_SCAN_PROTO_UDP && s_port_scan_records[i].status==PORT_SCAN_STATUS_OPEN);
    assert(s_port_scan_records[i].port==ports[i] && !strcmp(s_port_scan_records[i].ip_str,"10.0.0.1"));
  }
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_OK);
  dispatch(SPI_ID_WIFI_PORT_SCAN_STOP,NULL,0,NULL,NULL); assert(tcp_cancelled && probe_cancelled);
  runner(); runner_busy=false; dispatch(SPI_ID_WIFI_UDP_SCAN_STATUS,NULL,0,(void *)&status,&length);
  assert(!status.is_running && status.error==ESP_ERR_INVALID_STATE && s_port_scan_count==0 && log_count==4);
  runner_fails=true;
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_BUSY);
  dispatch(SPI_ID_WIFI_UDP_SCAN_STATUS,NULL,0,(void *)&status,&length);
  assert(!status.is_running && status.error==ESP_ERR_INVALID_STATE);
  runner_fails=false; reset(); fault=1;
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_OK);
  runner(); runner_busy=false; dispatch(SPI_ID_WIFI_UDP_SCAN_STATUS,NULL,0,(void *)&status,&length);
  assert(status.error==ESP_ERR_NO_MEM && !status.is_running && s_port_scan_count==0 && log_count==0);
  reset(); uint16_t mixed[]={10000,10001,10002,10004}; memcpy(payload+20,mixed,sizeof(mixed));
  assert(dispatch(SPI_ID_WIFI_UDP_SCAN_START,payload,28,NULL,NULL)==SPI_STATUS_OK);
  runner(); runner_busy=false;
  assert(log_count==4 && s_port_scan_count==4 && now==5000000);
  assert(!strcmp(log_lines[2],"HIT: 10.0.0.1:10004 [UDP] FILTERED | ICMP destination unreachable"));
  assert(!strcmp(log_lines[3],"HIT: 10.0.0.1:10002 [UDP] OPEN|FILTERED | No UDP reply"));
}

int main(void) {
  reset(); const int ports[]={53,123,10000,10001,10002,10003,10004,10005,10006,10007};
  assert(wifi_udp_scan_run(NULL)==ESP_ERR_INVALID_ARG);
  assert(run(ports,0)==ESP_ERR_INVALID_ARG && sockets==0);
  assert(run(ports,33)==ESP_ERR_INVALID_ARG && sockets==0);
  int bad[]={0,65536,-1}; for(unsigned i=0;i<3;++i) assert(run(bad+i,1)==ESP_ERR_INVALID_ARG);
  wifi_udp_scan_cancel(); assert(run(ports,10)==ESP_ERR_INVALID_STATE && sockets==0);
  reset(); assert(run(ports,10)==ESP_OK && received_count==8 && peak_fds==5);
  assert(results[0].status==WIFI_UDP_SCAN_OPEN && !strcmp(results[0].banner,"OK..."));
  assert(results[3].port==10001 && results[3].status==WIFI_UDP_SCAN_OPEN && results[3].banner[0]==0);
  unsigned opens=0,unknowns=0,filtered=0;
  for(unsigned i=0;i<received_count;++i) {
    opens+=results[i].status==WIFI_UDP_SCAN_OPEN;
    unknowns+=results[i].status==WIFI_UDP_SCAN_OPEN_FILTERED;
    filtered+=results[i].status==WIFI_UDP_SCAN_FILTERED;
  }
  assert(opens==5 && unknowns==2 && filtered==1);
  assert(sends==13 && now>=2000000);
  reset(); mode=1; int many[32]; for(unsigned i=0;i<32;++i) many[i]=20000+i;
  assert(run(many,32)==ESP_OK && received_count==32 && sends==64 && now==8000000);
  reset(); mode=2; assert(run(many,32)==ESP_OK && received_count==32 && sends==32 && now==400000);
  reset(); mode=1; fault=10; assert(run(many,4)==ESP_OK && received_count==4 && now==1000000);
  reset(); cancel_after=1; assert(run(ports,10)==ESP_ERR_INVALID_STATE && received_count==4 && sends==4);
  for(unsigned i=0;i<received_count;++i) assert(results[i].status==WIFI_UDP_SCAN_OPEN);
  reset(); cancel_send=1; assert(run(ports,10)==ESP_ERR_INVALID_STATE && received_count==0 && sends==1);
  for(unsigned f=1;f<=12;++f) if(f!=10) {
    reset(); fault=f; int err=run(f==12 ? ports+5 : ports, f==12 ? 1 : 4);
    assert(err==(f==1 ? ESP_ERR_NO_MEM : ESP_FAIL));
    assert(received_count==0);
  }
  reset(); test_parser(); reset(); test_dispatcher();
  puts("PASS: UDP reply/zero-byte/timeout/retry/ICMP classification, bounded parser, four-socket batches, cancellation, allocation/socket failure cleanup and SPI ownership");
}
