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

"""Run the saved-network flow through production P4, wire decoding and C5 functions."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/nfc_api'))
from test_emu_tx import extract
P4 = ROOT / 'firmware_p4/components'
C5 = ROOT / 'firmware_c5/components'
c5 = (C5 / 'Service/wifi/wifi_service.c').read_text()
p4 = (P4 / 'Service/wifi/wifi_service.c').read_text()
bridge = (P4 / 'Service/spi_bridge/spi_bridge.c').read_text()
dispatch = (C5 / 'Service/spi_bridge/wifi_dispatcher.c').read_text()
ui = (P4 / 'Applications/ui/screens/connect_wifi/connect_wifi_ui.c').read_text()
notice = c5.split('#include', 1)[0]
cjson = ROOT / 'tools/dependencies/build/managed_components/espressif__cjson/cJSON'
assets = (C5 / 'Service/storage_assets/storage_assets.c').read_text()

BACKEND = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "cJSON.h"
#include "spi_protocol.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_NO_MEM 0x101
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define SSID_MAX_LEN 32
#define PASSWORD_MAX_LEN 64
#define WIFI_SSID_MAX_LEN 32
#define WIFI_SSID_BUF_LEN 33
#define WIFI_PASS_BUF_LEN 65
#define WIFI_KNOWN_NETWORKS_FILE "storage/wifi/know_networks.json"
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_IF_STA 0
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_AP_STACONNECTED 1
#define WIFI_EVENT_AP_STADISCONNECTED 2
#define WIFI_EVENT_STA_DISCONNECTED 3
#define IP_EVENT_AP_STAIPASSIGNED 4
#define IP_EVENT_STA_GOT_IP 5
typedef int esp_event_base_t;
typedef struct { uint8_t mac[6]; } wifi_event_ap_staconnected_t;
typedef struct { struct {char ssid[32], password[64]; struct {int authmode;} threshold;
 struct {bool capable, required;} pmf_cfg;} sta;} wifi_config_t;
static wifi_config_t configured;
static bool s_is_connected;
static bool in_event;
static char *stored;
static int writes, requests, configurations, spi_calls, connect_error, config_error;
static bool write_fails, read_fails, snapshot_fails;
static bool s_is_initialized = true;
static bool storage_assets_is_mounted(void) { assert(!in_event);return true; }
static int storage_assets_init(void) { assert(!in_event);return ESP_OK; }
static int storage_assets_get_file_size(const char *name, size_t *size) {
 assert(!in_event);
 assert(!strcmp(name,WIFI_KNOWN_NETWORKS_FILE));*size=stored ? strlen(stored) : 0;
 return stored ? ESP_OK : ESP_ERR_NOT_FOUND;
}
static int storage_assets_read_file(const char *name,uint8_t *data,size_t size,size_t *out_read) {
 assert(!in_event && !strcmp(name,WIFI_KNOWN_NETWORKS_FILE));
 assert(stored && size==strlen(stored) && size>0);
 if(read_fails)return ESP_FAIL;
 memcpy(data,stored,size);*out_read=size;return ESP_OK;
}
static int storage_assets_write_file(const char *name,const char *data) {
 assert(!in_event);
 assert(!strcmp(name,WIFI_KNOWN_NETWORKS_FILE));
 if(write_fails)return ESP_FAIL;
 free(stored);stored=strdup(data);assert(stored);writes++;return ESP_OK;
}
static int esp_wifi_disconnect(void) { s_is_connected=false;return ESP_OK; }
static int esp_wifi_set_config(int interface,const wifi_config_t *cfg) {
 assert(interface==WIFI_IF_STA);configurations++;if(config_error)return config_error;
 configured=*cfg;return ESP_OK;
}
static int esp_wifi_connect(void) { requests++;return connect_error; }
static int esp_wifi_get_config(int interface,wifi_config_t *out) {
 assert(interface==WIFI_IF_STA);if(snapshot_fails)return ESP_FAIL;*out=configured;return ESP_OK;
}
/* WORKER_FIXTURES */
/* ASSET_LOAD */
static uint32_t spi_bridge_get_timeout(spi_id_t id) { assert(id==SPI_ID_WIFI_CONNECT);return 1000; }
static void save_known_network(const char *ssid,const char *password);
static bool get_known_network_password(const char *ssid,char *out_password,size_t buffer_size);
static void known_network_writer_task(void *arg);
static void queue_known_network(void);
static esp_err_t init_known_network_writer(void);
/* C5 */
/* STATUS */
static spi_status_t decode(const uint8_t *payload,size_t len) {
 switch(SPI_ID_WIFI_CONNECT) {
 /* DISPATCH */
 default: return SPI_STATUS_ERROR;
 }
}
static esp_err_t spi_bridge_send_command(spi_id_t id,uint8_t *payload,size_t len,
 void *header,void *out,size_t capacity,uint32_t timeout) {
 assert(id==SPI_ID_WIFI_CONNECT && !header && !out && !capacity && timeout);
 spi_calls++;return status_to_err(decode(payload,len));
}
/* P4 */
static void got_ip_event(void) {
 int before=writes;in_event=true;
 event_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
 in_event=false;assert(writes==before && s_is_connected);
}
static void drain_writer(void) {
 if(setjmp(worker_idle)==0)worker_fn(worker_arg);
}
static void got_ip(void) { got_ip_event();drain_writer(); }
static void expect_password(const char *ssid,const char *password) {
 char actual[65];assert(get_known_network_password(ssid,actual,sizeof(actual)));
 assert(!strcmp(actual,password));
}
int main(void) {
 // Initialization failures leave no published queue and permit a later retry.
 got_ip_event();assert(!writes && !s_known_network_queue);
 atomic_store(&s_is_known_network_starting,true);
 assert(init_known_network_writer()==ESP_ERR_INVALID_STATE);
 atomic_store(&s_is_known_network_starting,false);
 fail_allocate=true;assert(init_known_network_writer()==ESP_ERR_NO_MEM);
 assert(!s_known_network_queue && !s_is_known_network_starting && !tasks_created);
 fail_allocate=false;fail_queue=true;assert(init_known_network_writer()==ESP_ERR_NO_MEM);
 assert(!s_known_network_queue && !s_is_known_network_starting && !tasks_created);
 fail_queue=false;fail_task=true;assert(init_known_network_writer()==ESP_ERR_NO_MEM);
 assert(!s_known_network_queue && !s_is_known_network_starting && queues_deleted==1);
 fail_task=false;
 assert(p4_connect("Home",NULL)==ESP_ERR_NOT_FOUND);
 // The installed placeholder is a zero-byte file, not a missing file.
 stored=strdup("");assert(stored);
 assert(p4_connect("Home",NULL)==ESP_ERR_NOT_FOUND);
 assert(!requests && !configurations && !writes);
 assert(p4_connect("Home","first-password")==ESP_OK);
 assert(tasks_created==1 && init_known_network_writer()==ESP_OK && tasks_created==1);
 assert(!writes && !strcmp(configured.sta.password,"first-password"));
 assert(configured.sta.threshold.authmode==WIFI_AUTH_WPA2_PSK);
 event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
 assert(!writes);
 got_ip();assert(writes==1);expect_password("Home","first-password");
 got_ip();assert(writes==1);
 // Radio off/on or a C5 restart does not discard persistent credentials.
 s_is_connected=false;memset(&configured,0,sizeof(configured));
 assert(p4_connect("Home",NULL)==ESP_OK);
 assert(!strcmp(configured.sta.password,"first-password"));
 assert(writes==1);got_ip();assert(writes==1);
 // A failed replacement must preserve the last working password.
 assert(p4_connect("Home","wrong-password")==ESP_OK);
 event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
 expect_password("Home","first-password");
 assert(p4_connect("Home",NULL)==ESP_OK);assert(!strcmp(configured.sta.password,"first-password"));
 assert(p4_connect("Home","new-password")==ESP_OK);got_ip();
 assert(writes==2);expect_password("Home","new-password");
 // An explicit empty password never falls through to a saved password.
 assert(p4_connect("Home","")==ESP_OK);
 assert(!configured.sta.password[0] && configured.sta.threshold.authmode==WIFI_AUTH_OPEN);
 expect_password("Home","new-password");got_ip();expect_password("Home","");
 assert(p4_connect("Home",NULL)==ESP_OK && !configured.sta.password[0]);
 char ssid[33], password[65];memset(ssid,'S',32);ssid[32]=0;memset(password,'a',64);password[64]=0;
 assert(p4_connect(ssid,password)==ESP_OK);got_ip();expect_password(ssid,password);
 memset(&configured,0,sizeof(configured));assert(p4_connect(ssid,NULL)==ESP_OK);
 assert(!memcmp(configured.sta.ssid,ssid,32) && !memcmp(configured.sta.password,password,64));
 // Reject truncated/oversize wire requests before any copy or radio operation.
 uint8_t payload[128]={0};int before=requests;
 for(size_t size=0;size<=sizeof(payload);size++) {
  if(size==32 || size==96)continue;
  assert(decode(payload,size)==SPI_STATUS_INVALID_ARG);
 }
 assert(requests==before);
 before=spi_calls;assert(p4_connect(NULL,NULL)==ESP_ERR_INVALID_ARG);
 assert(p4_connect("",NULL)==ESP_ERR_INVALID_ARG);
 char long_ssid[34];memset(long_ssid,'x',33);long_ssid[33]=0;
 char long_pass[66];memset(long_pass,'x',65);long_pass[65]=0;
 assert(p4_connect(long_ssid,NULL)==ESP_ERR_INVALID_ARG);
 assert(p4_connect("Home",long_pass)==ESP_ERR_INVALID_ARG);assert(spi_calls==before);
 before=writes;connect_error=ESP_FAIL;assert(p4_connect("Home","rejected")==ESP_FAIL);
 assert(writes==before);connect_error=0;config_error=ESP_FAIL;
 assert(p4_connect("Home","rejected")==ESP_FAIL && writes==before);config_error=0;
 write_fails=true;assert(p4_connect("Home","not-persisted")==ESP_OK);got_ip();
 expect_password("Home","");write_fails=false;
 read_fails=true;before=writes;save_known_network("Home","not-persisted");
 assert(writes==before);read_fails=false;expect_password("Home","");
 // Snapshot is independent of radio state and remains valid after another request.
 assert(p4_connect("Queued","confirmed-password")==ESP_OK);
 got_ip_event();assert(p4_connect("NeverConnected","unsuccessful-password")==ESP_OK);
 event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
 memset(&configured,0,sizeof(configured));drain_writer();
 expect_password("Queued","confirmed-password");
 char missing[65];assert(!get_known_network_password("NeverConnected",missing,sizeof(missing)));
 // A full queue never blocks sys_evt or overwrites previously accepted saves.
 before=writes;
 for(unsigned i=0;i<KNOWN_NETWORK_QUEUE_LENGTH;i++) {
  char name[33];snprintf(name,sizeof(name),"Queued-%u",i);
  assert(p4_connect(name,"queue-password")==ESP_OK);got_ip_event();
 }
 assert(p4_connect("Overflow","not-saved")==ESP_OK);got_ip_event();
 assert(writes==before && queued==KNOWN_NETWORK_QUEUE_LENGTH && tasks_created==1);
 drain_writer();assert(writes==before+KNOWN_NETWORK_QUEUE_LENGTH);
 assert(!get_known_network_password("Overflow",missing,sizeof(missing)));
 got_ip();expect_password("Overflow","not-saved");
 before=writes;
 for(unsigned i=0;i<KNOWN_NETWORK_QUEUE_LENGTH;i++)got_ip_event();
 drain_writer();assert(writes==before);
 assert(p4_connect("SnapshotFailure","not-saved")==ESP_OK);
 snapshot_fails=true;got_ip();snapshot_fails=false;
 assert(!get_known_network_password("SnapshotFailure",missing,sizeof(missing)));
 // Do not destroy an existing malformed file while trying to append a network.
 free(stored);stored=strdup("{bad-json");save_known_network("Home","new-password");
 assert(!strcmp(stored,"{bad-json"));char out[65];
 assert(!get_known_network_password("Home",out,sizeof(out)));
 assert(!get_known_network_password("Home",out,0));free(stored);
 free(queue_items);
 puts("PASS: saved credentials, deferred FIFO writes outside sys_evt, snapshot ownership, queue/task allocation failures, saturation, retries, failed replacement and wire bounds");
}
'''
WORKER_FIXTURES = r'''
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define SYS_PRIO_BACKGROUND 3
#define SYS_CORE_MAIN 0
typedef struct { unsigned unused; } StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
/* WORKER_STATE */
static bool fail_allocate, fail_queue, fail_task;
static unsigned queued, tasks_created, queues_deleted;
static uint8_t *queue_items;
static void (*worker_fn)(void *);
static void *worker_arg;
static jmp_buf worker_idle;
static void *heap_caps_malloc(size_t size,unsigned caps) {
 assert(!in_event && size>0 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 void *data=malloc(size);assert(data);memset(data,0xa5,size);return data;
}
static void *heap_caps_calloc(size_t count,size_t size,unsigned caps) {
 assert(!in_event && count==KNOWN_NETWORK_QUEUE_LENGTH && size==sizeof(wifi_service_known_network_t));
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 return fail_allocate ? NULL : calloc(count,size);
}
static QueueHandle_t xQueueCreateStatic(unsigned count,size_t size,uint8_t *items,StaticQueue_t *control) {
 assert(!in_event && count==KNOWN_NETWORK_QUEUE_LENGTH && size==sizeof(wifi_service_known_network_t));
 assert(items && control==&s_known_network_queue_storage);
 if(fail_queue)return NULL;
 queue_items=items;queued=0;return control;
}
static void vQueueDelete(QueueHandle_t queue) {
 assert(queue==&s_known_network_queue_storage && !queued);queues_deleted++;
}
static int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,
 unsigned priority,void *handle,int core) {
 assert(!in_event && !strcmp(name,"wifi_save") && stack>=4096);
 assert(arg==&s_known_network_queue_storage && priority==SYS_PRIO_BACKGROUND && !handle && core==SYS_CORE_MAIN);
 if(fail_task)return 0;
 worker_fn=fn;worker_arg=arg;tasks_created++;return pdPASS;
}
static int xQueueSend(QueueHandle_t queue,const void *item,unsigned wait) {
 assert(in_event && queue==s_known_network_queue && wait==0);
 if(queued==KNOWN_NETWORK_QUEUE_LENGTH)return 0;
 memcpy(queue_items+queued*sizeof(wifi_service_known_network_t),item,sizeof(wifi_service_known_network_t));
 queued++;return pdTRUE;
}
static int xQueueReceive(QueueHandle_t queue,void *item,unsigned wait) {
 assert(!in_event && queue==s_known_network_queue && wait==portMAX_DELAY);
 if(!queued)longjmp(worker_idle,1);
 memcpy(item,queue_items,sizeof(wifi_service_known_network_t));queued--;
 memmove(queue_items,queue_items+sizeof(wifi_service_known_network_t),queued*sizeof(wifi_service_known_network_t));
 return pdTRUE;
}
'''
worker_state = c5[c5.index('typedef struct {\n  char ssid[SSID_MAX_LEN + 1];'):c5.index('static wifi_ap_record_t s_stored_aps')]
worker_defines = '\n'.join(re.findall(r'^#define KNOWN_NETWORK_.*$', c5, re.MULTILINE))
BACKEND = BACKEND.replace('/* WORKER_FIXTURES */', WORKER_FIXTURES.replace('/* WORKER_STATE */', worker_defines+'\n'+worker_state))
BACKEND = BACKEND.replace('/* ASSET_LOAD */', extract(assets, 'uint8_t *storage_assets_load_file(const char *filename, size_t *out_size) {'))

backend = BACKEND.replace('/* C5 */','\n'.join(extract(c5,sig) for sig in (
 'esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password) {',
 'static esp_err_t init_known_network_writer(void) {',
 'static void known_network_writer_task(void *arg) {',
 'static void queue_known_network(void) {',
 'static void save_known_network(const char *ssid, const char *password) {',
 'static bool get_known_network_password(const char *ssid, char *out_password, size_t buffer_size) {',
 'static void\nevent_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {')))
backend=backend.replace('/* P4 */',extract(p4,'esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password) {').replace('wifi_service_connect_to_ap(', 'p4_connect('))
backend=backend.replace('/* STATUS */',extract(bridge,'static esp_err_t status_to_err(spi_status_t status) {'))
backend=backend.replace('/* DISPATCH */',dispatch[dispatch.index('    case SPI_ID_WIFI_CONNECT: {'):dispatch.index('    case SPI_ID_WIFI_DISCONNECT:')])

UI = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_FAIL -1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdPASS 1
#define SYS_PRIO_SERVICE_LO 4
#define SYS_CORE_RADIO 0
#define KEYBOARD_PASSWORD 1
#define SCREEN_CONNECT_WIFI 4
#define LV_SYMBOL_OK "ok"
#define LV_SYMBOL_CLOSE "close"
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdMS_TO_TICKS(ms) (ms)
typedef int esp_err_t,portMUX_TYPE,lv_timer_t,lv_obj_t;
typedef struct {const char *title,*initial;int max_chars,min_chars,mode;} keyboard_config_t;
/* DEFINES */
static char s_connect_ssid[WIFI_SSID_BYTES+1],s_connect_pass[WIFI_PASSWORD_BYTES+1];
static bool s_use_saved_password,s_is_secured,s_connect_cancelled,s_connecting,s_scanning;
static portMUX_TYPE s_connect_lock;
static lv_timer_t *s_connect_timer=(void*)1;
static lv_obj_t *s_join_footer=(void*)2;
static uint8_t s_connect_state,s_connect_ip[4];
static int current_screen=SCREEN_CONNECT_WIFI,connect_error,keyboards,messages,elapsed;
static bool task_fails,cancel_on_delay,connected;
static const char *reported_ssid;
static char received_pass[65];static bool received_null;
static void (*task_fn)(void*);static void (*keyboard_cb)(const char*,void*);
static void (*message_cb)(bool);
static int ui_current_screen(void) { return current_screen; }
static void led_signal_info(void) {} static void led_signal_warning(void) {} static void led_signal_error(void) {}
static void ui_chrome_footer_set_text(lv_obj_t *obj,const char *text) { assert(obj && text); }
static void lv_timer_delete(lv_timer_t *timer) { assert(timer); }
static void keyboard_open_config(const keyboard_config_t *cfg,void (*cb)(const char*,void*),void *ctx) {
 assert(cfg->initial==NULL && cfg->max_chars==64 && !ctx);keyboard_cb=cb;keyboards++;
}
static void msgbox_open(const char *icon,const char *msg,const char *ok,const char *cancel,void (*cb)(bool)) {
 (void)icon;(void)msg;(void)ok;(void)cancel;messages++;message_cb=cb;
}
static int xTaskCreatePinnedToCore(void (*fn)(void*),const char *name,int stack,void *arg,int priority,void *handle,int core) {
 assert(name && stack==8192 && !arg && priority==SYS_PRIO_SERVICE_LO && !handle && core==SYS_CORE_RADIO);
 task_fn=fn;return task_fails ? 0 : pdPASS;
}
static void vTaskDelete(void *task) { assert(!task); }
void ui_connect_wifi_close(void);
static void vTaskDelay(int ms) { elapsed+=ms;if(cancel_on_delay)ui_connect_wifi_close(); }
static int wifi_service_connect_to_ap(const char *ssid,const char *pass) {
 assert(!strcmp(ssid,"Home"));received_null=pass==NULL;
 strcpy(received_pass,pass ? pass : "");return connect_error;
}
static const char *wifi_service_get_connected_ssid(void) { return connected ? reported_ssid : NULL; }
static int wifi_service_get_ip(int interface,uint8_t *ip) { assert(interface==0);memcpy(ip,(uint8_t[]){192,168,1,2},4);return ESP_OK; }
static void on_keyboard_submit(const char *text,void *user_data);
/* FUNCTIONS */
static void reset(void) {
 s_connect_timer=(void*)1;s_join_footer=(void*)2;s_connecting=false;s_connect_cancelled=false;
 s_scanning=false;s_connect_state=CONNECT_STATE_IDLE;strcpy(s_connect_ssid,"Home");s_is_secured=true;
 connect_error=keyboards=messages=elapsed=0;task_fails=cancel_on_delay=connected=false;
 reported_ssid="Home";current_screen=SCREEN_CONNECT_WIFI;
}
int main(void) {
 reset();connect_error=ESP_ERR_NOT_FOUND;start_connection(NULL);assert(connection_busy());
 task_fn(NULL);assert(received_null && !connection_busy() && !keyboards);
 connect_done_cb(NULL);assert(keyboards==1);keyboard_cb("first-password",NULL);
 assert(connection_busy());connect_error=0;connected=true;task_fn(NULL);connect_done_cb(NULL);
 assert(messages==1 && !strcmp(received_pass,"first-password") && !s_connect_pass[0]);
 reset();connected=true;start_connection(NULL);task_fn(NULL);connect_done_cb(NULL);
 assert(received_null && !keyboards && messages==1);
 reset();start_connection(NULL);task_fn(NULL);connect_done_cb(NULL);
 assert(elapsed==CONNECT_TIMEOUT_MS && message_cb);message_cb(true);assert(keyboards==1);
 reset();connected=true;reported_ssid="Other";start_connection(NULL);task_fn(NULL);
 assert(s_connect_state==CONNECT_STATE_FAILED);
 reset();start_connection("");connected=true;task_fn(NULL);connect_done_cb(NULL);
 assert(!received_null && !received_pass[0]);
 reset();task_fails=true;start_connection(NULL);assert(!connection_busy());connect_done_cb(NULL);assert(messages==1);
 reset();start_connection(NULL);cancel_on_delay=true;task_fn(NULL);
 assert(!s_connect_timer && !connection_busy() && s_connect_state==CONNECT_STATE_IDLE);
 connect_done_cb(NULL);assert(!keyboards && !messages);ui_connect_wifi_close();
 // A late missing-password result after navigation never opens a keyboard.
 reset();connect_error=ESP_ERR_NOT_FOUND;start_connection(NULL);ui_connect_wifi_close();
 task_fn(NULL);connect_done_cb(NULL);assert(!keyboards && !messages);
 reset();s_connect_timer=NULL;start_connection(NULL);assert(!connection_busy());
 puts("PASS: known-network UI skips keyboard, missing/changed password prompts, stale SSID rejected, timeout, task failure, Back and late completion");
}
'''
defines='\n'.join(line for line in ui.splitlines() if re.match(r'#define (CONNECT_|WIFI_TASK|WIFI_SSID_BYTES|WIFI_PASSWORD_BYTES|JOIN_HINT)',line))
ui_test=UI.replace('/* DEFINES */',defines).replace('/* FUNCTIONS */','\n'.join(extract(ui,sig) for sig in (
 'static bool connection_busy(void) {','static bool connection_cancelled(void) {',
 'static void edit_password(bool confirm) {','static void connect_done_cb(lv_timer_t *timer) {',
 'static void wifi_connect_task(void *arg) {','static void start_connection(const char *password) {',
 'static void on_keyboard_submit(const char *text, void *user_data) {','void ui_connect_wifi_close(void) {')))
assert 'start_connection(s_is_secured ? NULL : "")' in ui
assert 'case SCREEN_CONNECT_WIFI:\n      return ui_connect_wifi_close;' in (P4/'Applications/ui/ui_manager.c').read_text()
with tempfile.TemporaryDirectory(prefix='highboy-saved-wifi-') as tmp:
 path=Path(tmp)
 (path/'esp_rom_crc.h').write_text(notice+'static inline uint16_t esp_rom_crc16_le(uint16_t crc,const void *data,unsigned len) {return crc;}\n')
 for name,source in [('backend',backend),('ui',ui_test)]:
  (path/f'{name}.c').write_text(notice+source)
  args=[os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-deprecated-declarations',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(path),'-I'+str(P4/'Service/spi_bridge/include'),
        '-I'+str(cjson),str(path/f'{name}.c')]
  if name=='backend':args.append(str(cjson/'cJSON.c'))
  subprocess.run(args+['-o',str(path/name)],check=True)
  subprocess.run([str(path/name)],check=True,timeout=20)
