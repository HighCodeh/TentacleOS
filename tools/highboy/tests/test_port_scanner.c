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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "port_scanner.h"
#include "spi_protocol.h"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define SYS_PRIO_SERVICE_LO 4
#define SYS_CORE_RADIO 0
#define pdPASS 1
#define pdMS_TO_TICKS(value) (value)
#define RES_HANDLE_NONE 0
#define RES_WIFI 1
#define RES_OWNER_UI 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
typedef int res_handle_t;
typedef struct { int id, owner_kind; void (*on_revoke)(void *); } res_request_t;
static int64_t clock_us;
static unsigned starts, stops, polls, batches, scanned_ports, allocs, fail_alloc;
static unsigned cancel_batch, busy_polls, polls_left, lost_polls, remove_sd_batch;
static uint16_t fail_result_index;
static bool sd_mounted;
static unsigned probes, cancel_probe, probe_reply_every;
static int probe_error;
static bool bad_probe_status;
static unsigned probe_hosts_total, bad_probe_reply;
static unsigned probe_fail_at;
static bool fail_host_map, expect_all_ports;
static size_t host_map_bytes;
static uint32_t last_tcp_host;
static uint16_t last_tcp_port;
static void check_tcp_phase(void);
static spi_wifi_host_probe_batch_req_t active_probe;
static unsigned probe_counts[32];
static uint32_t probe_firsts[32];
static bool run_on_delay, radio_fails, power_fails, bad_count;
static bool task_fails, has_radio, has_power, disconnected, all_open, bad_frame, refused;
static char last_ip[16];
static uint16_t active_ports[32], active_count;
static void (*pending)(void *);
static void *pending_arg;
static int worker_stack_caps;
static uint32_t source_ip, source_mask, source_gateway;
static int64_t esp_timer_get_time(void) { return clock_us; }
static void vTaskDelay(unsigned delay) {
  clock_us += delay * 1000LL;
  if (run_on_delay && pending != NULL) {
    void (*fn)(void *)=pending; pending=NULL; fn(pending_arg);
  }
}
static void led_signal_info(void) {}
static void led_signal_error(void) {}
static void led_signal_warning(void) {}
static void *heap_caps_calloc(size_t n, size_t size, int caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if(size==1) {
    host_map_bytes=n;
    if(fail_host_map) return NULL;
  }
  return ++allocs == fail_alloc ? NULL : calloc(n, size);
}
static int xTaskCreatePinnedToCoreWithCaps(void (*fn)(void *), const char *name, unsigned stack,
    void *argument, int priority, void *handle, int core, int caps) {
  assert(priority == SYS_PRIO_SERVICE_LO && core == SYS_CORE_RADIO);
  if (task_fails) return 0;
  worker_stack_caps = caps;
  pending = fn; pending_arg = argument; return pdPASS;
}
static void vTaskDeleteWithCaps(void *unused) {}
static esp_err_t resource_acquire(const res_request_t *request, res_handle_t *handle) {
  assert(!has_radio); if (radio_fails) return ESP_FAIL; has_radio = true; *handle = 1; return ESP_OK;
}
static esp_err_t resource_release(res_handle_t handle) {
  assert(has_radio && polls_left == 0 && lost_polls == 0); has_radio = false; return ESP_OK;
}
static esp_err_t power_manager_no_sleep_acquire(void) { if (power_fails) return ESP_FAIL; has_power = true; return ESP_OK; }
static esp_err_t power_manager_no_sleep_release(void) { assert(has_power); has_power = false; return ESP_OK; }
int common_ports_load(int *ports, int capacity) {
  // The assets fallback enters the SDK flash path with the caller's stack.
  assert((worker_stack_caps & MALLOC_CAP_INTERNAL) != 0);
  assert((worker_stack_caps & MALLOC_CAP_SPIRAM) == 0);
  assert(capacity >= 3); ports[0]=22; ports[1]=80; ports[2]=80; return 3;
}
static esp_err_t spi_bridge_send_command(spi_id_t id, const void *data, uint8_t length,
    spi_header_t *header, void *output, size_t capacity, unsigned timeout) {
  assert(has_radio && has_power); header->length = capacity;
  if (id == SPI_ID_WIFI_GET_IP_INFO) {
    spi_wifi_ip_info_t info = {.ip=disconnected ? 0 : source_ip, .netmask=source_mask, .gw=source_gateway};
    memcpy(output, &info, sizeof(info)); return ESP_OK;
  }
  if (id == SPI_ID_WIFI_HOST_PROBE_BATCH_START) {
    assert(starts==0 && polls_left==0);
    assert(length==sizeof(active_probe)); memcpy(&active_probe,data,sizeof(active_probe));
    assert(active_probe.count>0 && active_probe.count<=8 && active_probe.reserved[0]==0);
    probe_counts[probes%32]=active_probe.count; probe_firsts[probes%32]=ntohl(active_probe.first_ip);
    probe_hosts_total+=active_probe.count;
    ++probes; polls_left=busy_polls;
    if (cancel_probe == probes) port_scanner_stop();
    return ESP_OK;
  }
  if (id == SPI_ID_WIFI_HOST_PROBE_BATCH_STATUS) {
    assert(polls_left==0);
    spi_wifi_host_probe_batch_status_t status={.error=probes==probe_fail_at ? ESP_FAIL : probe_error,
      .first_ip=active_probe.first_ip,.count=active_probe.count,.is_running=bad_probe_status};
    for(unsigned i=0;i<active_probe.count;++i)
      if(probe_reply_every && (probe_hosts_total-active_probe.count+i+1)%probe_reply_every==0)
        status.reachable_mask|=1U<<i;
    if(bad_probe_reply==1) ++status.first_ip;
    if(bad_probe_reply==2) --status.count;
    if(bad_probe_reply==3) status.reachable_mask|=1U<<active_probe.count;
    assert(capacity==sizeof(status)); memcpy(output,&status,sizeof(status)); return ESP_OK;
  }
  if (id == SPI_ID_WIFI_PORT_SCAN_TARGET_LIST) {
    check_tcp_phase();
    assert(polls_left==0);
    ++starts; if (refused) return ESP_ERR_INVALID_STATE;
    const uint8_t *payload = data; uint16_t limit;
    memcpy(last_ip, payload, 16); memcpy(&limit, payload+16, 2); memcpy(&active_count, payload+18, 2);
    assert(limit == 32 && active_count > 0 && active_count <= limit && length == 20 + active_count*2);
    memcpy(active_ports, payload+20, active_count*2);
    for (unsigned i=1; i<active_count; ++i) assert(active_ports[i] > active_ports[i-1]);
    uint32_t host=ntohl(inet_addr(last_ip));
    if(starts>1) {
      assert(host>=last_tcp_host);
      if(host==last_tcp_host) assert(active_ports[0]>last_tcp_port);
      else if(expect_all_ports) assert(last_tcp_port==65535);
    }
    last_tcp_host=host; last_tcp_port=active_ports[active_count-1];
    ++batches; scanned_ports += active_count; polls_left = busy_polls;
    if (remove_sd_batch==batches) sd_mounted=false;
    if (cancel_batch == batches) port_scanner_stop();
    return ESP_OK;
  }
  if (id == SPI_ID_WIFI_PORT_SCAN_STOP) { ++stops; polls_left=0; return ESP_OK; }
  if (id == SPI_ID_WIFI_SCAN_STATUS) {
    ++polls;
    if (lost_polls) { --lost_polls; return ESP_ERR_TIMEOUT; }
    *(uint8_t *)output = polls_left ? 1 : 0;
    if (polls_left) --polls_left;
    return ESP_OK;
  }
  assert(id == SPI_ID_SYSTEM_DATA); uint16_t index; memcpy(&index, data, 2);
  if (index == SPI_DATA_INDEX_COUNT) { *(uint16_t *)output = bad_count ? active_count+1 : all_open ? active_count : 0; return ESP_OK; }
  assert(index < active_count && capacity == sizeof(spi_port_scan_result_t));
  if (index==fail_result_index) return ESP_ERR_TIMEOUT;
  spi_port_scan_result_t result = {.port=active_ports[index], .protocol=0, .status=0};
  memcpy(result.ip_str, last_ip, 16); memset(result.banner, 'A', sizeof(result.banner));
  memcpy(output, &result, sizeof(result)); if (bad_frame) --header->length;
  return ESP_OK;
}
static unsigned write_calls, fail_write, flush_calls, fail_flush, close_calls, fail_close;
static bool storage_is_mounted(void) { return sd_mounted; }
static esp_err_t storage_mkdir_recursive(const char *path) {
  return mkdir(path, 0700)==0 || errno==EEXIST ? ESP_OK : ESP_FAIL;
}
static FILE *file_open(const char *path, const char *mode) {
  if(!sd_mounted) { errno=ENODEV; return NULL; }
  return fopen(path,mode);
}
static size_t file_write(const void *data, size_t size, size_t count, FILE *file) {
  if(++write_calls==fail_write) return 0;
  return fwrite(data,size,count,file);
}
static int file_flush(FILE *file) {
  int result=fflush(file); return ++flush_calls==fail_flush ? EOF : result;
}
static int file_close(FILE *file) {
  int result=fclose(file); return ++close_calls==fail_close ? EOF : result;
}
#define fopen file_open
#define fwrite file_write
#define fflush file_flush
#define fclose file_close
#define TAG ATOMIC_TAG
/* ATOMIC */
#undef TAG
#define TAG STORE_TAG
/* STORE */
#undef TAG
#undef fopen
#undef fwrite
#undef fflush
#undef fclose
/* PRODUCTION */
static void check_tcp_phase(void) {
  assert(!s_status.is_probing);
  if(s_status.is_ping_enabled) {
    assert(s_status.is_discovery_complete);
    assert(s_status.hosts_probed==s_status.hosts_total);
    assert(probe_hosts_total==s_status.hosts_total);
  }
}
#define PORT_SCAN_DEFAULT_LAST 1024
#define PORT_SCAN_POLL_MS 200
static struct { int count; const char *sval[1]; } ip_arg;
static struct { int count; int ival[1]; } min_arg, max_arg;
static struct { int count; } common_arg;
static struct {
  __typeof__(ip_arg) *ip;
  __typeof__(min_arg) *min, *max;
  __typeof__(common_arg) *common;
  void *end;
} s_port_args={&ip_arg,&min_arg,&max_arg,&common_arg,NULL};
static int arg_parse(int argc, char **argv, void **table) { return 0; }
static void arg_print_errors(FILE *stream, void *end, const char *name) {}
/* CONSOLE */
static void reset(void) {
  assert(!s_status.is_busy && !s_is_page_busy && !has_radio && !has_power);
  probe_hosts_total=bad_probe_reply=0;
  probe_fail_at=0; fail_host_map=expect_all_ports=false;
  host_map_bytes=0; last_tcp_host=0; last_tcp_port=0;
  probes=cancel_probe=probe_reply_every=0; probe_error=ESP_OK; bad_probe_status=false;
  sd_mounted=false; write_calls=fail_write=flush_calls=fail_flush=close_calls=fail_close=0;
  starts=stops=polls=batches=scanned_ports=allocs=fail_alloc=0;
  cancel_batch=busy_polls=polls_left=lost_polls=remove_sd_batch=0;
  fail_result_index=UINT16_MAX;
  radio_fails=power_fails=bad_count=false;
  task_fails=disconnected=all_open=bad_frame=refused=false; pending=NULL; clock_us=0;
  worker_stack_caps=0;
  source_ip=inet_addr("10.23.4.5"); source_mask=inet_addr("255.255.255.0"); source_gateway=inet_addr("10.23.4.1");
}
static void run(const port_scanner_config_t *config) {
  expect_all_ports=config->port_mode==PORT_SCANNER_ALL_PORTS;
  assert(port_scanner_start(config)==ESP_OK);
  assert(port_scanner_start(config)==ESP_ERR_INVALID_STATE);
  assert(pending != NULL); void (*fn)(void *)=pending; pending=NULL; fn(pending_arg);
  assert(!has_radio && !has_power && !s_status.is_busy);
}
static void run_page(const char *path, uint32_t page, port_scanner_page_t *out) {
  assert(port_scanner_request_page(path,page)==ESP_OK);
  port_scanner_get_page(out); assert(out->is_busy);
  assert(port_scanner_request_page(path,page)==ESP_ERR_INVALID_STATE);
  assert(pending!=NULL); void (*fn)(void *)=pending; pending=NULL; fn(pending_arg);
  port_scanner_get_page(out); assert(!out->is_busy && !has_power);
}

static void browse(const port_scanner_browse_request_t *request, port_scanner_page_t *out) {
  assert(port_scanner_browse(request)==ESP_OK);
  void (*fn)(void *)=pending; pending=NULL; fn(pending_arg);
  port_scanner_get_page(out); assert(!out->is_busy && !has_power);
}

static void test_archive_groups(void) {
  reset(); sd_mounted=true;
  char path[PORT_SCANNER_PATH_LENGTH], other[PORT_SCANNER_PATH_LENGTH];
  assert(port_scanner_store_create(path,sizeof(path),0x0a020001,0x0a020011)==ESP_OK);
  port_scan_result_t result={.ip_str="10.2.0.1",.port=80,.protocol=PORT_SCAN_PROTO_TCP,
                             .status=PORT_SCAN_STATUS_OPEN,.banner="HTTP"};
  assert(port_scanner_store_append(path,&result,1)==ESP_OK);
  FILE *file=fopen(path,"r+b"); assert(file);
  char row[RECORD_BYTES]; assert(fseek(file,sizeof(HEADER)-1,SEEK_SET)==0);
  assert(fread(row,1,sizeof(row),file)==sizeof(row));
  for(unsigned i=1;i<65535;++i) assert(fwrite(row,1,sizeof(row),file)==sizeof(row));
  assert(fclose(file)==0);
  for(unsigned i=2;i<=17;++i) {
    snprintf(result.ip_str,sizeof(result.ip_str),"10.2.0.%u",i);
    assert(port_scanner_store_append(path,&result,1)==ESP_OK);
    result.port=443; assert(port_scanner_store_append(path,&result,1)==ESP_OK); result.port=80;
  }
  port_scanner_browse_request_t request={.kind=PORT_SCANNER_BROWSE_HOSTS};
  strcpy(request.path,path);
  port_scanner_page_t page;
  browse(&request,&page);
  assert(page.error==ESP_OK && page.count==8 && page.has_next && page.cursor==0);
  assert(page.hosts[0].count==65535 && page.hosts[1].count==2);
  assert(!strcmp(page.hosts[7].ip,"10.2.0.8") && page.next_cursor==65549);
  port_scanner_host_t host=page.hosts[1];
  request.number=1; request.cursor=page.next_cursor; browse(&request,&page);
  assert(page.count==8 && !strcmp(page.hosts[0].ip,"10.2.0.9") && page.has_next);
  request.number=2; request.cursor=page.next_cursor; browse(&request,&page);
  assert(page.count==1 && !strcmp(page.hosts[0].ip,"10.2.0.17") && !page.has_next);
  request.number=1; request.cursor=page.cursor; request.is_previous=true; browse(&request,&page);
  assert(page.count==8 && !strcmp(page.hosts[0].ip,"10.2.0.9") && page.cursor==65549);
  request.number=0; request.cursor=page.cursor; browse(&request,&page);
  assert(page.count==8 && page.cursor==0 && page.hosts[0].count==65535);
  request.kind=PORT_SCANNER_BROWSE_PORTS; request.host=host;
  browse(&request,&page); assert(page.error==ESP_OK && page.count==2 && page.total==2);
  assert(!strcmp(page.results[1].ip_str,"10.2.0.2") && page.results[1].port==443);
  request.number=1; browse(&request,&page); assert(page.count==0 && !page.has_next);
  request.number=0; strcpy(request.host.ip,"10.2.0.3"); browse(&request,&page);
  assert(page.error==ESP_ERR_INVALID_RESPONSE);
  request.host=host; request.host.count=UINT32_MAX; browse(&request,&page);
  assert(page.error==ESP_ERR_INVALID_RESPONSE);
  request.kind=PORT_SCANNER_BROWSE_HOSTS; request.cursor=UINT32_MAX; browse(&request,&page);
  assert(page.error==ESP_ERR_INVALID_ARG);
  request.cursor=0; request.is_previous=false;
  assert(port_scanner_browse(&request)==ESP_OK); port_scanner_close_page();
  void (*fn)(void *)=pending; pending=NULL; fn(pending_arg);
  assert(!s_is_page_busy && !has_power);
  char label[PORT_SCANNER_TARGET_LENGTH];
  target_label("scan_c0a80001_c0a800fe.csv",label,sizeof(label)); assert(!strcmp(label,"192.168.0.0/24"));
  target_label("scan_0a000001_0a00fffe.csv",label,sizeof(label)); assert(!strcmp(label,"10.0.0.0/16"));
  target_label("scan_0a000001_0a000001.csv",label,sizeof(label)); assert(!strcmp(label,"10.0.0.1"));
  target_label("scan_0a00000a_0a000014.csv",label,sizeof(label)); assert(!strcmp(label,"10.0.0.10-10.0.0.20"));
  target_label("scan_20261123_123456_00000001.csv",label,sizeof(label)); assert(strstr(label,"Legacy"));
  assert(port_scanner_store_create(other,sizeof(other),0xc0a80001,0xc0a800fe)==ESP_OK);
  assert(strcmp(path,other));
  char unrelated[PORT_SCANNER_PATH_LENGTH]; strcpy(unrelated,other);
  // A replacement header must be fully written before the previous file is replaced.
  fail_write=write_calls+1;
  assert(port_scanner_store_create(other,sizeof(other),0x0a020001,0x0a020011)==ESP_FAIL);
  browse(&request,&page); assert(page.error==ESP_OK && page.hosts[0].count==65535);
  fail_write=0;
  assert(port_scanner_store_create(other,sizeof(other),0x0a020001,0x0a020011)==ESP_OK);
  assert(!strcmp(path,other)); browse(&request,&page); assert(page.error==ESP_OK && page.count==0);
  request.kind=PORT_SCANNER_BROWSE_DELETE;
  assert(port_scanner_browse(&request)==ESP_OK);
  port_scanner_config_t config={.target_mode=PORT_SCANNER_HOST,.target="10.2.0.1"};
  assert(port_scanner_start(&config)==ESP_ERR_INVALID_STATE);
  port_scanner_close_page(); fn=pending; pending=NULL; fn(pending_arg);
  assert(access(path,F_OK)==0);
  strcpy(s_status.archive_path,path); s_status.stored=1; s_status.saved=1;
  browse(&request,&page); assert(page.error==ESP_OK && access(path,F_OK)!=0);
  assert(access(unrelated,F_OK)==0);
  assert(!s_status.archive_path[0] && !s_status.stored && !s_status.saved);
  browse(&request,&page); assert(page.error!=ESP_OK);
  strcpy(request.path,"/tmp/do-not-delete.csv"); browse(&request,&page);
  assert(page.error==ESP_ERR_INVALID_ARG);
  sd_mounted=false; strcpy(request.path,path); browse(&request,&page);
  assert(page.error==ESP_ERR_INVALID_STATE);
}
static bool never_cancelled(void) { return false; }
int main(void) {
  port_scanner_config_t config = {.target_mode=PORT_SCANNER_CIDR, .port_mode=PORT_SCANNER_CUSTOM_PORTS, .target="10.2.3.4/16", .ports="22,80,8000-8100"};
  uint32_t first, last;
  assert(port_scanner_validate(&config)==ESP_OK && target_bounds(&config,&first,&last)==ESP_OK);
  assert(first==0x0a020001 && last==0x0a02fffe);
  strcpy(config.target,"10.2.3.4/31"); assert(target_bounds(&config,&first,&last)==ESP_OK && last-first==1);
  strcpy(config.target,"10.2.3.4/32"); assert(target_bounds(&config,&first,&last)==ESP_OK && last==first);
  const char *invalid[] = {"10.2.3.4/33","10.2.3.4/-1","10.2.3.4/4294967297","999.2.3.4/24","10.2.3.4/24x"};
  for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) { strcpy(config.target,invalid[i]); assert(port_scanner_validate(&config)==ESP_ERR_INVALID_ARG); }
  config.target_mode=PORT_SCANNER_GATEWAY;
  const char *bad_ports[]={"0","65536","80-22","22,","22,,80","4294967297","-1",""};
  for(unsigned i=0;i<sizeof(bad_ports)/sizeof(*bad_ports);++i) { strcpy(config.ports,bad_ports[i]); assert(port_scanner_validate(&config)==ESP_ERR_INVALID_ARG); }
  config.port_mode=PORT_SCANNER_COMMON_PORTS;
  reset(); fail_alloc=1; assert(port_scanner_start(&config)==ESP_ERR_NO_MEM);
  reset(); fail_alloc=2; assert(port_scanner_start(&config)==ESP_ERR_NO_MEM);
  reset(); task_fails=true; assert(port_scanner_start(&config)==ESP_ERR_NO_MEM);
  assert(s_status.state==PORT_SCANNER_FAILED && !s_status.is_busy && pending==NULL);
  assert(!has_radio && !has_power && starts==0);
  reset(); run(&config); assert(s_status.state==PORT_SCANNER_DONE && !strcmp(last_ip,"10.23.4.1") && scanned_ports==2);
  reset(); config.target_mode=PORT_SCANNER_LOCAL; run(&config);
  assert(s_status.state==PORT_SCANNER_DONE && s_status.hosts_done==254 && scanned_ports==508);
  config.target_mode=PORT_SCANNER_GATEWAY;
  reset(); radio_fails=true; run(&config); assert(s_status.error==ESP_FAIL && starts==0);
  reset(); power_fails=true; run(&config); assert(s_status.error==ESP_FAIL && starts==0);
  reset(); bad_count=true; run(&config); assert(s_status.error==ESP_ERR_INVALID_RESPONSE);
  reset(); disconnected=true; run(&config); assert(s_status.state==PORT_SCANNER_FAILED && starts==0);
  reset(); refused=true; run(&config); assert(s_status.state==PORT_SCANNER_FAILED && stops==0);
  reset(); all_open=true; bad_frame=true; run(&config); assert(s_status.error==ESP_ERR_INVALID_RESPONSE);
  reset(); lost_polls=3; run(&config); assert(s_status.error==ESP_ERR_TIMEOUT && stops>=3);
  reset(); config.target_mode=PORT_SCANNER_LOCAL; config.port_mode=PORT_SCANNER_CUSTOM_PORTS; strcpy(config.ports,"80");
  run(&config); assert(s_status.hosts_done==254 && batches==254 && !strcmp(last_ip,"10.23.4.254"));
  reset(); config.target_mode=PORT_SCANNER_RANGE; strcpy(config.target,"10.0.0.1-10.0.0.5"); strcpy(config.ports,"1-32"); all_open=true;
  run(&config); assert(s_status.hosts_done==5 && scanned_ports==160 && s_status.found==160 && s_status.stored==128);
  port_scan_result_t result; assert(port_scanner_get_result(127,&result) && result.banner[63]=='\0'); assert(!port_scanner_get_result(128,&result));
  reset(); busy_polls=60; run(&config); assert(clock_us>=60000000 && stops==0 && s_status.state==PORT_SCANNER_DONE);
  reset(); busy_polls=300; run(&config); assert(s_status.error==ESP_ERR_TIMEOUT && stops>0);
  reset(); config.target_mode=PORT_SCANNER_CIDR; strcpy(config.target,"10.0.0.0/16"); cancel_batch=3;
  run(&config); assert(s_status.hosts_total==65534 && s_status.state==PORT_SCANNER_CANCELLED && batches==3 && s_status.hosts_done==2 && stops>0);
  reset(); config.target_mode=PORT_SCANNER_HOST; strcpy(config.target,"10.0.0.1"); config.port_mode=PORT_SCANNER_ALL_PORTS;
  run(&config); assert(scanned_ports==65535 && s_status.ports_done==65535 && s_status.hosts_done==1);
  reset(); config.port_mode=PORT_SCANNER_CUSTOM_PORTS; strcpy(config.ports,"80,22,22,79-81"); run(&config); assert(scanned_ports==4);
  reset(); run_on_delay=true; ip_arg.count=1; ip_arg.sval[0]="10.23.4.1";
  common_arg.count=1; assert(subcmd_portscan(0,NULL)==0 && scanned_ports==2);
  reset(); common_arg.count=0; min_arg.count=max_arg.count=1;
  min_arg.ival[0]=20; max_arg.ival[0]=84;
  assert(subcmd_portscan(0,NULL)==0 && scanned_ports==65);
  reset(); max_arg.ival[0]=-1; assert(subcmd_portscan(0,NULL)==1 && starts==0);
  reset(); disconnected=true; max_arg.ival[0]=80; assert(subcmd_portscan(0,NULL)==1 && starts==0);
  reset(); sd_mounted=true; config=(port_scanner_config_t){.target_mode=PORT_SCANNER_RANGE,
      .port_mode=PORT_SCANNER_CUSTOM_PORTS,.target="10.0.0.1-10.0.0.5",.ports="1-32"};
  all_open=true; run(&config);
  assert(s_status.found==160 && s_status.stored==128 && s_status.saved==160);
  assert(port_scanner_get_result(0,&result) && !strcmp(result.ip_str,"10.0.0.2") && result.port==1);
  assert(port_scanner_get_result(127,&result) && !strcmp(result.ip_str,"10.0.0.5") && result.port==32);
  assert(s_status.storage_error==ESP_OK && s_status.archive_path[0]);
  char archive[PORT_SCANNER_PATH_LENGTH]; strcpy(archive,s_status.archive_path);
  port_scanner_page_t page;
  run_page(archive,19,&page); assert(page.error==ESP_OK && page.count==8 && page.total==160);
  assert(!strcmp(page.results[7].ip_str,"10.0.0.5") && page.results[7].port==32);
  assert(strlen(page.results[7].banner)==63);
  assert(page.results[7].banner[0]=='A');
  run_page(archive,UINT32_MAX,&page); assert(page.error==ESP_OK && page.count==0 && page.total==160);
  run_page(NULL,0,&page); assert(page.error==ESP_OK && page.total==1 && page.count==1);
  assert(strstr(archive,page.files[0]));
  reset(); sd_mounted=true; all_open=true; cancel_batch=2; run(&config);
  assert(s_status.state==PORT_SCANNER_CANCELLED && s_status.saved==64);
  assert(!strcmp(archive,s_status.archive_path));
  run_page(archive,0,&page); assert(page.total==64 && page.count==8);
  // Close/re-entry and reboot-like loss of the RAM preview still read the archive.
  free(s_results); s_results=NULL; s_status=(port_scanner_snapshot_t){0};
  run_page(archive,7,&page); assert(page.total==64 && page.results[7].port==32);
  // An index beyond 16 bits uses a direct file seek, without an in-memory index.
  FILE *large=fopen(archive,"r+b"); assert(large);
  char row[RECORD_BYTES]; assert(fseek(large,sizeof(HEADER)-1,SEEK_SET)==0);
  assert(fread(row,1,sizeof(row),large)==sizeof(row));
  assert(fseek(large,(sizeof(HEADER)-1)+65536L*RECORD_BYTES,SEEK_SET)==0);
  assert(fwrite(row,1,sizeof(row),large)==sizeof(row)); assert(fclose(large)==0);
  run_page(archive,8192,&page); assert(page.error==ESP_OK && page.total==65537 && page.count==1);
  assert(page.results[0].port==1);
  large=fopen(archive,"ab"); assert(large); assert(fwrite("partial",1,7,large)==7); fclose(large);
  run_page(archive,8192,&page); assert(page.error==ESP_OK && page.is_partial && page.count==1);
  assert(port_scanner_store_append(archive,page.results,1)==ESP_ERR_INVALID_SIZE);
  run_page("/sdcard/../outside.csv",0,&page); assert(page.error==ESP_ERR_INVALID_ARG);
  reset(); sd_mounted=true; all_open=true; fail_write=3; run(&config);
  assert(s_status.state==PORT_SCANNER_FAILED && s_status.storage_error!=ESP_OK && s_status.saved==0);
  assert(batches==1); run_page(s_status.archive_path,0,&page);
  assert(page.error==ESP_OK && page.count==1);
  reset(); sd_mounted=true; all_open=true; remove_sd_batch=2; run(&config);
  assert(s_status.state==PORT_SCANNER_FAILED && s_status.saved==32 && batches==2);
  sd_mounted=true; run_page(s_status.archive_path,0,&page); assert(page.total==32);
  reset(); sd_mounted=true; all_open=true; fail_result_index=2; run(&config);
  assert(s_status.state==PORT_SCANNER_FAILED && s_status.saved==2 && s_status.found==2);
  run_page(s_status.archive_path,0,&page); assert(page.total==2 && page.results[1].port==2);
  reset(); sd_mounted=true; all_open=true; fail_flush=2; run(&config);
  assert(s_status.state==PORT_SCANNER_FAILED && s_status.storage_error!=ESP_OK && s_status.saved==0);
  reset(); sd_mounted=true; all_open=true; fail_close=2; run(&config);
  assert(s_status.state==PORT_SCANNER_FAILED && s_status.storage_error!=ESP_OK && s_status.saved==0);
  reset(); sd_mounted=true; all_open=true; fail_write=1; run(&config);
  assert(s_status.state==PORT_SCANNER_DONE && s_status.storage_error!=ESP_OK && !s_status.archive_path[0]);
  assert(s_status.stored==128 && s_status.saved==0);
  reset(); sd_mounted=true;
  assert(port_scanner_request_page(NULL,0)==ESP_OK);
  assert(port_scanner_start(&config)==ESP_ERR_INVALID_STATE);
  port_scanner_close_page(); void (*fn)(void *)=pending; pending=NULL; fn(pending_arg);
  assert(!s_is_page_busy && !has_power);
  reset(); fail_alloc=1; assert(port_scanner_request_page(NULL,0)==ESP_ERR_NO_MEM);
  assert(!s_is_page_busy && !has_power);
  reset(); task_fails=true; assert(port_scanner_request_page(NULL,0)==ESP_ERR_NO_MEM);
  assert(!s_is_page_busy && !has_power);
  reset(); sd_mounted=true; power_fails=true; run_page(NULL,0,&page); assert(page.error==ESP_FAIL);
  reset(); run_page(NULL,0,&page); assert(page.error==ESP_ERR_INVALID_STATE);
  // A damaged record is reported instead of displaying arbitrary bytes.
  large=fopen(archive,"r+b"); assert(large); assert(fseek(large,sizeof(HEADER)-1,SEEK_SET)==0);
  assert(fwrite("bad",1,3,large)==3); fclose(large);
  page=(port_scanner_page_t){0}; sd_mounted=true;
  assert(port_scanner_store_read(archive,&page,never_cancelled)==ESP_ERR_INVALID_RESPONSE);
  free(s_results); s_results=NULL;
  free(s_page_data); s_page_data=NULL;
  port_scanner_config_t ping={.target_mode=PORT_SCANNER_RANGE,
      .port_mode=PORT_SCANNER_CUSTOM_PORTS, .target="10.0.0.1-10.0.0.4", .ports="22,80",
      .is_ping_enabled=true};
  reset(); probe_reply_every=2; busy_polls=2; run(&ping);
  assert(probes==1 && batches==2 && scanned_ports==4 && s_status.hosts_done==4);
  assert(s_status.hosts_skipped==2 && s_status.hosts_responded==2 && !s_status.is_probing);
  reset(); run(&ping); assert(probes==1 && batches==0 && s_status.hosts_skipped==4);
  reset(); ping.is_ping_enabled=false; run(&ping);
  assert(probes==0 && batches==4 && s_status.hosts_skipped==0);
  ping.is_ping_enabled=true;
  reset(); cancel_probe=1; busy_polls=5; run(&ping);
  assert(probes==1 && batches==0 && stops==1 && s_status.state==PORT_SCANNER_CANCELLED);
  reset(); probe_error=ESP_FAIL; run(&ping);
  assert(probes==1 && batches==0 && s_status.state==PORT_SCANNER_FAILED);
  reset(); bad_probe_status=true; run(&ping);
  assert(batches==0 && s_status.error==ESP_ERR_INVALID_RESPONSE);
  for(unsigned kind=1;kind<=3;++kind) {
    reset(); bad_probe_reply=kind; run(&ping);
    assert(batches==0 && s_status.error==ESP_ERR_INVALID_RESPONSE);
  }
  reset(); strcpy(ping.target,"10.0.0.1-10.0.0.19"); probe_reply_every=2; all_open=true;
  sd_mounted=true; run(&ping);
  assert(probes==3 && probe_hosts_total==19 && batches==9 && s_status.saved==18);
  assert(probe_counts[0]==8 && probe_counts[1]==8 && probe_counts[2]==3);
  assert(probe_firsts[0]==0x0a000001 && probe_firsts[1]==0x0a000009 && probe_firsts[2]==0x0a000011);
  assert(s_status.hosts_done==19 && s_status.hosts_skipped==10 && s_status.hosts_responded==9);
  assert(s_status.hosts_probed==19 && s_status.is_discovery_complete && host_map_bytes==3);
  strcpy(archive,s_status.archive_path);
  reset(); sd_mounted=true; fail_host_map=true; run(&ping);
  assert(s_status.error==ESP_ERR_NO_MEM && probes==0 && starts==0);
  run_page(archive,0,&page); assert(page.total==18);
  reset(); probe_reply_every=1; probe_fail_at=2; run(&ping);
  assert(probes==2 && starts==0 && s_status.error==ESP_FAIL && !s_status.is_discovery_complete);
  assert(s_status.hosts_probed==8 && s_status.hosts_responded==8);
  reset(); strcpy(ping.target,"10.0.0.1-10.0.0.254"); busy_polls=4; run(&ping);
  assert(probes==32 && probe_hosts_total==254 && batches==0 && s_status.hosts_skipped==254);
  assert(clock_us==25600000 && s_status.hosts_done==254);
  reset(); strcpy(ping.target,"10.0.0.1-10.0.0.19"); probe_reply_every=1;
  cancel_probe=2; busy_polls=4; all_open=true; sd_mounted=true; run(&ping);
  assert(probes==2 && batches==0 && s_status.saved==0 && s_status.state==PORT_SCANNER_CANCELLED);
  assert(s_status.hosts_done==0 && s_status.hosts_skipped==0 && s_status.hosts_probed==8);
  assert(!s_status.is_discovery_complete);
  run_page(archive,0,&page); assert(page.total==0);
  reset(); probe_reply_every=2; cancel_batch=2; all_open=true; sd_mounted=true; run(&ping);
  assert(probes==3 && probe_hosts_total==19 && batches==2 && s_status.saved==4);
  assert(s_status.state==PORT_SCANNER_CANCELLED && s_status.is_discovery_complete);
  run_page(archive,0,&page); assert(page.total==4 && !strcmp(page.results[3].ip_str,"10.0.0.4"));
  reset(); strcpy(ping.target,"10.0.0.1-10.0.0.9"); ping.port_mode=PORT_SCANNER_ALL_PORTS;
  probe_reply_every=4; busy_polls=2; run(&ping);
  assert(probes==2 && scanned_ports==2*65535 && batches==4096 && s_status.hosts_done==9);
  assert(s_status.ports_done==65535 && last_tcp_host==0x0a000008);
  reset(); ping.target_mode=PORT_SCANNER_CIDR; strcpy(ping.target,"10.0.0.0/16");
  ping.port_mode=PORT_SCANNER_CUSTOM_PORTS; probe_reply_every=8192; run(&ping);
  assert(host_map_bytes==8192 && probes==8192 && probe_hosts_total==65534);
  assert(batches==7 && s_status.hosts_done==65534 && s_status.hosts_probed==65534);
  assert(s_status.hosts_responded==7 && s_status.hosts_skipped==65527);
  reset(); strcpy(ping.target,"0.0.0.0/0"); fail_host_map=true; run(&ping);
  assert(host_map_bytes==536870912 && s_status.error==ESP_ERR_NO_MEM && probes==0 && starts==0);
  ping.target_mode=PORT_SCANNER_RANGE;
  reset(); strcpy(ping.target,"255.255.255.254-255.255.255.255"); run(&ping);
  assert(probes==1 && probe_hosts_total==2 && s_status.hosts_done==2);
  test_archive_groups();
  puts("PASS: grouped archive/IP/port paging, large hosts, deterministic replacement, partial cancellation, checked deletion, scanner bounds, ping batches, I/O errors and ownership");
}
